/* A22 - KIT: Studio FM + Hybrid ADPCM Percussion Synthesizer.
 *
 * 12 drums per octave across the 27-key keyboard (upper octaves shift pitch naturally):
 *   C Kick  C# Rim  D Snare  D# Clap  E LoTom  F MidTom  F# Hat  G HiTom
 *   G# Shaker  A Cowbell/Bell  A# OpenHat (choked by Hat)  B Crash/Ride
 *
 * Combines 2-op exponential FM percussion, multi-mode ZDF noise, 6-osc metallic clusters,
 * and hybrid CC0 acoustic IMA ADPCM transients (plunk_data.c) on CRISP / LOFI / WORLD kits.
 *
 *   K1 (BLUE)   TUNE     global kit pitch (-12 .. +12 semitones)
 *   K2 (GREEN)  DECAY    snappy tight gate .. booming sustain
 *   K3 (WHITE)  PUNCH    pitch-drop snap, transient click & punch compression
 *   K4 (ORANGE) FM-GRIT  FM modulation index, saturation & lo-fi crunch */
#include "internal.h"
#include "dsp.h"
#include "plunk_data.h"

enum { KICK, RIM, SNARE, CLAP, TOML, TOMM, HAT, TOMH, SHAKE, COW, OHAT, CRASH, NDRUM };

typedef struct DrumDef {
    f32 f0, sweep, tsw, tdec, tone;
    f32 nlev, nfc, nq, ndec;
    f32 mlev, mdec, fm_rat, fm_idx;
    uint8_t nmode;                               /* 0 LP, 1 BP, 2 HP */
    uint8_t adpcm_zone;                          /* acoustic transient zone from plunk_data */
} DrumDef;

static const DrumDef DR[NDRUM] = {
    /* f0     swp   tsw    tdec  tone  nlev  nfc   nq    ndec   mlev  mdec  fmr    fmi   nm zone */
    { 50.0f, 2.5f, 0.028f, 0.36f, 1.0f, 0.14f, 2600, 0.8f, 0.014f, 0.0f, 0.0f, 1.50f, 0.35f, 0, VZ_BASSDRUM},
    {490.0f, 0.35f,0.005f, 0.025f,0.75f,0.45f, 3400, 2.6f, 0.015f, 0.25f,0.02f,2.76f, 0.60f, 1, VZ_STICK},
    {188.0f, 0.85f,0.016f, 0.12f, 0.68f,0.95f, 4400, 0.75f,0.16f,  0.0f, 0.0f, 2.41f, 0.55f, 1, VZ_SNARE},
    {  0.0f, 0.0f, 0.01f,  0.01f, 0.0f, 1.05f, 1350, 1.7f, 0.18f,  0.0f, 0.0f, 1.00f, 0.00f, 1, VZ_CLAP},
    { 90.0f, 1.0f, 0.048f, 0.30f, 1.0f, 0.12f, 1000, 0.8f, 0.035f, 0.0f, 0.0f, 1.41f, 0.40f, 0, VZ_TUMBA},
    {138.0f, 0.9f, 0.042f, 0.24f, 1.0f, 0.12f, 1350, 0.8f, 0.030f, 0.0f, 0.0f, 1.41f, 0.45f, 0, VZ_QUINTO},
    {  0.0f, 0.0f, 0.01f,  0.01f, 0.0f, 0.28f, 9200, 0.85f,0.032f, 0.92f,0.040f,3.14f,0.80f, 2, VZ_HAT},
    {210.0f, 0.8f, 0.036f, 0.19f, 1.0f, 0.12f, 1800, 0.8f, 0.028f, 0.0f, 0.0f, 1.41f, 0.45f, 0, VZ_BONGO},
    {  0.0f, 0.0f, 0.01f,  0.01f, 0.0f, 0.85f, 7000, 1.5f, 0.065f, 0.0f, 0.0f, 1.00f, 0.00f, 1, VZ_SHAKER},
    {545.0f, 0.05f,0.01f,  0.22f, 0.35f,0.0f,  2000, 1.0f, 0.01f,  0.95f,0.22f,1.44f, 0.70f, 1, VZ_COWBELL},
    {  0.0f, 0.0f, 0.01f,  0.01f, 0.0f, 0.24f, 9200, 0.85f,0.30f,  0.92f,0.44f,3.14f, 0.80f, 2, VZ_TAMB},
    {  0.0f, 0.0f, 0.01f,  0.01f, 0.0f, 0.50f, 6200, 0.65f,0.85f,  0.82f,1.05f,2.76f, 0.90f, 2, VZ_CYMBAL},
};

/* world kit alternate ADPCM zones */
static const uint8_t WORLD_ZONES[NDRUM] = {
    VZ_DUM, VZ_TEK, VZ_CAJON, VZ_CLAP, VZ_TUMBA, VZ_QUINTO,
    VZ_CABASA, VZ_BONGO, VZ_SHAKER, VZ_AGOGO, VZ_MBIRA, VZ_GONG
};

typedef struct KitDef {
    f32 pitch, sweep, dec, noise, bright, fm, crush, smp_mix;
    uint8_t mode;   /* 0:808, 1:909, 2:CYCLES FM, 3:CRISP hybrid, 4:LOFI, 5:MODULAR, 6:METAL, 7:WORLD */
} KitDef;

static const KitDef KITS[NALGO] = {
    /* 0 808     */ {0.96f, 1.15f, 1.35f, 0.85f, 0.92f, 0.25f, 0.00f, 0.00f, 0},
    /* 1 909     */ {1.12f, 1.55f, 0.78f, 1.15f, 1.30f, 0.55f, 0.00f, 0.22f, 1},
    /* 2 CYCLES  */ {1.05f, 1.20f, 1.00f, 0.55f, 1.15f, 2.20f, 0.00f, 0.00f, 2},
    /* 3 CRISP   */ {1.00f, 1.10f, 0.95f, 0.50f, 1.10f, 0.35f, 0.00f, 0.72f, 3},
    /* 4 LOFI    */ {0.90f, 0.95f, 0.88f, 0.90f, 0.68f, 0.40f, 0.58f, 0.55f, 4},
    /* 5 MODULAR */ {1.40f, 2.10f, 0.95f, 0.40f, 1.25f, 1.85f, 0.08f, 0.00f, 5},
    /* 6 METAL   */ {1.25f, 0.75f, 1.15f, 0.75f, 1.45f, 3.10f, 0.22f, 0.15f, 6},
    /* 7 WORLD   */ {1.00f, 0.45f, 1.10f, 0.18f, 1.00f, 0.35f, 0.00f, 0.92f, 7},
};

static const f32 METAL_RAT[6] = {1.0f, 1.4471f, 1.6170f, 1.9265f, 2.5028f, 2.6637f};

static struct {
    int algo;
    f32 tune, dec, punch, grit;
} D;
static uint32_t kit_hat_gen;
static f32 kit_t_was;

static void kit_block(const Blk *b)
{
    if (b->time < kit_t_was) kit_hat_gen = 0;
    kit_t_was = b->time;
    D.algo = b->algo & 7;
    D.tune = (b->k[0] - 0.5f) * 24.0f;
    D.dec  = exp2fx((b->k[1] - 0.5f) * 4.8f);
    D.punch = b->k[2];
    D.grit  = b->k[3];
}

static void kit_note(Voice *v, const Blk *b)
{
    int type = v->note % 12, i;
    const KitDef *kit = &KITS[D.algo];
    uint32_t zone = kit->mode == 7 ? WORLD_ZONES[type] : DR[type].adpcm_zone;
    (void)b;
    if (!v->u[0]) v->u[0] = 0xDB0C0001u + v->idx * 1013u;
    for (i = 0; i < 8; i++) v->ph[i] = fractf(v->r1 * (f32)(i + 1) * 0.31f);
    for (i = 0; i < 16; i++) v->s[i] = 0.0f;
    v->s[4] = v->s[5] = v->s[6] = v->s[7] = 1.0f;    /* tone, noise, metal, sweep envs */
    v->s[8] = (f32)type;
    v->s[10] = 1.0f;                                  /* ADPCM decay env */
    v->s[13] = 1.0f;                                  /* crush counter */
    v->u[1] = 0;                                      /* sample counter */
    v->u[3] = 0;                                      /* ADPCM frac */
    v->q[0] = 0;                                      /* ADPCM idx */
    v->q[1] = 0;                                      /* ADPCM pred */
    v->q[2] = plunk_zones[zone].idx0;                 /* ADPCM step */
    v->q[3] = zone;
    if (type == HAT) kit_hat_gen++;
    v->u[2] = kit_hat_gen;
    v->pan = (type == KICK || type == SNARE) ? 0.0f : clampf(((f32)type - 5.5f) * 0.11f, -0.62f, 0.62f);
}

static void kit_render(Voice *v, const Blk *b, f32 *out_l, f32 *out_r, int n)
{
    int type = iclamp((int)v->s[8], 0, NDRUM - 1), i, j;
    const DrumDef *d = &DR[type];
    const KitDef *k = &KITS[D.algo];
    f32 oct = ((f32)v->note - 60.0f - (f32)type) * (1.0f / 12.0f);
    f32 semis = D.tune + oct * 6.0f + b->bend;
    f32 ratio = k->pitch * exp2fx(semis * (1.0f / 12.0f));
    f32 vel = lerpf(1.0f, 0.35f + 0.85f * v->vel_f, b->vel_sens);
    f32 dec = D.dec * k->dec * (type == KICK && k->mode == 0 ? 1.45f : 1.0f);
    f32 f0 = d->f0 * ratio;
    f32 sweep = d->sweep * k->sweep * (0.40f + 1.30f * D.punch + 0.35f * b->punch);
    f32 kt = 1.0f - onepole_k(maxf(0.005f, d->tdec * dec), b->sr);
    f32 kn = 1.0f - onepole_k(maxf(0.005f, d->ndec * dec), b->sr);
    f32 km = 1.0f - onepole_k(maxf(0.005f, d->mdec * dec), b->sr);
    f32 ks = 1.0f - onepole_k(maxf(0.003f, d->tsw), b->sr);
    f32 at = v->s[4], an = v->s[5], am = v->s[6], sw = v->s[7];
    f32 ph = v->ph[6], pm = v->ph[7];
    f32 fmi = (d->fm_idx * k->fm + 1.8f * D.grit * D.grit) * vel;
    f32 nlev = d->nlev * k->noise * (1.0f + 0.5f * D.grit) * vel;
    f32 tone = d->tone * (1.0f - 0.72f * k->smp_mix * (k->mode == 7 ? 1.0f : (type == KICK ? 0.15f : 0.45f)));
    f32 mlev = d->mlev * (k->mode == 7 ? 0.15f : 1.0f);
    f32 mf = (type == COW ? d->f0 : 325.0f) * ratio * (type == CRASH ? 1.31f : 1.0f);
    f32 drive = 1.0f + 2.2f * D.punch + 3.2f * D.grit;
    f32 comp = 1.0f / (0.60f + 0.40f * drive);
    f32 click = (type == KICK || type == TOML || type == TOMM || type == TOMH) ? (0.45f * D.punch + 0.25f * b->punch) : 0.0f;
    f32 crush = clampf(k->crush + 0.65f * D.grit * D.grit, 0.0f, 0.92f);
    f32 hold = v->s[12], hc = v->s[13], hstep = 1.0f - crush * 0.92f;
    f32 gl, gr;
    int nmet = type == COW ? 2 : 6, clap = type == CLAP;
    Svf fn, fm;

    /* ADPCM acoustic transient layer setup */
    int use_smp = k->smp_mix > 0.01f;
    const PlunkZone *z = &plunk_zones[v->q[3] < VZ_N ? v->q[3] : 0];
    const uint8_t *zd = plunk_data + z->off;
    uint32_t z_inc = 0, z_frac = v->u[3] & 0xFFFFu, z_idx = v->q[0], z_step = v->q[2] > 88u ? 88u : v->q[2];
    int32_t z_pred = (int32_t)v->q[1];
    f32 z_env = v->s[10], z_kd = 1.0f, z_prev = v->s[11], z_cur = v->s[14];
    if (use_smp) {
        f32 sr_r = exp2fx(clampf(semis * (1.0f / 12.0f), -2.5f, 2.5f));
        z_inc = (uint32_t)(sr_r * (f32)z->rate * b->isr * 65536.0f);
        if (z_inc > (6u << 16)) z_inc = 6u << 16;
        z_kd = 1.0f - onepole_k(maxf(0.008f, z->tau * dec * (k->mode == 1 ? 0.35f : 1.0f)), b->sr);
        if (z_pred > 32767 || z_pred < -32768) z_pred = 0;
    }

    if (type == OHAT && v->u[2] != kit_hat_gen) {
        km = 0.992f;
        kn = 0.992f;
        z_kd = 0.992f;
    }
    svf_set(&fn, clampf(d->nfc * k->bright * (0.75f + 0.5f * vel), 120.0f, 16000.0f), d->nq, b->isr);
    svf_set(&fm, clampf(5400.0f * k->bright, 600.0f, 15500.0f), 0.85f, b->isr);
    pan_gains(v->pan, &gl, &gr);

    for (i = 0; i < n; i++) {
        f32 x = 0.0f, lo, bp;
        /* 2-op exponential FM drum body */
        if (tone > 0.0f && at > 0.0002f) {
            f32 f = minf(f0 * exp2fx(sweep * sw), 14000.0f) * b->isr;
            f32 m;
            ph += f; if (ph >= 1.0f) ph -= 1.0f;
            pm += minf(f * d->fm_rat, 0.44f); if (pm >= 1.0f) pm -= 1.0f;
            m = tsin(pm) * fmi * (0.25f + 0.75f * sw * at);
            x += tsin(ph + m) * tone * at;
            if (click > 0.0f && v->u[1] < 88u)
                x += click * (1.0f - (f32)v->u[1] * (1.0f / 88.0f)) * tsin((f32)v->u[1] * 0.18f);
            sw *= ks;
            at *= kt;
        }
        /* Filtered noise (snare wires, clap bursts, shaker) */
        if (nlev > 0.0f && an > 0.0002f) {
            f32 nz = noise_s(&v->u[0]);
            SVF_TICK(fn, &v->s[0], nz, lo, bp);
            x += nlev * an * (d->nmode == 0 ? lo : (d->nmode == 1 ? bp * 1.45f : (nz - lo - bp * fn.k)));
            an *= kn;
            if (clap && (v->u[1] == 390u || v->u[1] == 810u || v->u[1] == 1260u))
                an = 1.0f;
        }
        /* 6-oscillator metallic cluster (808/909 hats, cowbell, crash) */
        if (mlev > 0.0f && am > 0.0002f) {
            f32 met = 0.0f;
            for (j = 0; j < nmet; j++) {
                f32 p = v->ph[j] + mf * METAL_RAT[j] * b->isr;
                if (p >= 1.0f) p -= (f32)(int)p;
                v->ph[j] = p;
                met += p < 0.5f ? 1.0f : -1.0f;
            }
            met *= (nmet == 2 ? 0.5f : 0.22f);
            SVF_TICK(fm, &v->s[2], met, lo, bp);
            x += mlev * am * (type == COW ? bp * 1.35f : (met - lo - bp * fm.k));
            am *= km;
        }
        /* Hybrid acoustic ADPCM transient layer (CRISP / LOFI / WORLD / 909) */
        if (use_smp && z_env > 0.0004f) {
            f32 fr, sx;
            z_frac += z_inc;
            while (z_frac >= 65536u) {
                z_frac -= 65536u;
                z_prev = z_cur;
                if (z_idx >= z->len && z->loop < z->len) {
                    z_idx = z->loop;
                    z_pred = z->lpred;
                    z_step = z->lidx > 88u ? 88u : z->lidx;
                }
                if (z_idx < z->len) {
                    uint32_t nib = (zd[z_idx >> 1] >> ((z_idx & 1u) << 2)) & 15u, sz = plunk_steps[z_step];
                    int32_t q = (int32_t)(sz >> 3);
                    if (nib & 4u) q += (int32_t)sz;
                    if (nib & 2u) q += (int32_t)(sz >> 1);
                    if (nib & 1u) q += (int32_t)(sz >> 2);
                    z_pred = iclamp(nib & 8u ? z_pred - q : z_pred + q, -32768, 32767);
                    z_step = (uint32_t)iclamp((int32_t)z_step + ((nib & 4u) ? (int32_t)((nib & 3u) * 2u + 2u) : -1), 0, 88);
                    z_idx++;
                    z_cur = (f32)z_pred * (1.0f / 32768.0f);
                } else {
                    z_cur = 0.0f;
                }
            }
            fr = (f32)z_frac * (1.0f / 65536.0f);
            sx = (z_prev + (z_cur - z_prev) * fr) * z->gain * z_env * k->smp_mix * vel;
            z_env *= z_kd;
            x += sx;
        }
        v->u[1]++;
        x = softclip(x * drive) * comp;
        if (crush > 0.02f) {
            hc += hstep;
            if (hc >= 1.0f) {
                hc -= 1.0f;
                hold = (f32)ifloor(x * 28.0f + 0.5f) * (1.0f / 28.0f);
            }
            x = hold;
        }
        out_l[i] = x * gl;
        out_r[i] = x * gr;
    }

    v->s[4] = at; v->s[5] = an; v->s[6] = am; v->s[7] = sw;
    v->s[10] = z_env; v->s[11] = z_prev; v->s[12] = hold; v->s[13] = hc; v->s[14] = z_cur;
    v->ph[6] = ph; v->ph[7] = pm;
    v->u[3] = z_frac; v->q[0] = z_idx; v->q[1] = (uint32_t)z_pred; v->q[2] = z_step;
}

/* ---- 4-Color Kit Graphic: 12 Geometric Bauhaus Drum Pads + Sonar Rings --------- */
static f32 pad_hit[NDRUM], pad_pop[NDRUM], pad_popv[NDRUM];
static uint32_t db_seen[NVOICE];

static void kit_draw(const Vis *vis)
{
    static const int PCOL[NDRUM] = {
        GC_ORANGE, GC_WHITE, GC_WHITE, GC_GREEN,
        GC_ORANGE, GC_GREEN, GC_BLUE,  GC_GREEN,
        GC_BLUE,   GC_GOLD,  GC_BLUE,  GC_CYAN
    };
    f32 dt = vis->dt, tune = vis->k[0], dec = vis->k[1], punch = vis->k[2], grit = vis->k[3];
    int i;

    if (vis->fresh) {
        for (i = 0; i < NDRUM; i++) pad_hit[i] = pad_pop[i] = pad_popv[i] = 0.0f;
        for (i = 0; i < NVOICE; i++) db_seen[i] = vis->v[i].serial;
    }
    for (i = 0; i < NVOICE; i++) {
        if (vis->v[i].on && vis->v[i].serial != db_seen[i]) {
            int t = ((int)vis->v[i].note) % 12;
            db_seen[i] = vis->v[i].serial;
            pad_hit[t] = 1.0f;
            pad_popv[t] += 18.0f + 16.0f * punch;
        }
    }

    /* Clean 4x3 Kit Pad Matrix */
    for (i = 0; i < NDRUM; i++) {
        int col = i % 4, row = i / 4;
        f32 cx = 42.0f + (f32)col * 52.0f;
        f32 cy = 66.0f + (f32)row * 54.0f;
        f32 scale = (0.78f + 0.36f * tune) * (1.0f + 0.25f * pad_pop[i]);
        f32 r = (i == KICK ? 19.0f : (i == SNARE ? 17.0f : 14.0f)) * scale;
        f32 e = pad_hit[i];
        pad_hit[i] = maxf(0.0f, pad_hit[i] - dt / (0.14f + 0.75f * dec));
        spring_z(&pad_pop[i], &pad_popv[i], 0.0f, 26.0f, 0.35f, dt);

        /* subtle cell outline */
        g_color(GC_SLATE);
        g_box(cx - 22.0f, cy - 22.0f, 44.0f, 44.0f, 1.0f, 0.22f + 0.25f * e);

        g_color(PCOL[i]);
        if (i == KICK || i == TOML || i == TOMM || i == TOMH) {
            g_ring(cx, cy, r, 2.0f + 1.8f * punch, 0.45f + 0.55f * e);
            if (e > 0.05f) g_disc(cx, cy, r * e * 0.85f, 0.85f * e);
        } else if (i == SNARE || i == CLAP) {
            f32 tri[6] = {cx, cy - r, cx + r * 0.9f, cy + r * 0.75f, cx - r * 0.9f, cy + r * 0.75f};
            g_poly(tri, 3, 1, 2.0f + 1.5f * punch, 0.48f + 0.52f * e);
            if (e > 0.05f) g_fill(tri, 3, 0.55f * e);
        } else {
            /* metallic hats/perc: crossed diamond / burst */
            g_line(cx - r, cy, cx + r, cy, 1.8f + 1.2f * grit, 0.45f + 0.55f * e);
            g_line(cx, cy - r, cx, cy + r, 1.8f + 1.2f * grit, 0.45f + 0.55f * e);
            g_ring(cx, cy, r * 0.55f, 1.6f, 0.4f + 0.6f * e);
        }
        /* expanding sonar ring on strike */
        if (e > 0.02f)
            g_ring(cx, cy, r + (1.0f - e) * (16.0f + 22.0f * dec), 1.6f, e * 0.85f);
    }
    g_color(GC_WHITE);
}

const InstDef inst_kit = {
    "KIT",
    {255, 107, 26},
    {"TUNE", "DECAY", "PUNCH", "FM-GRIT"},
    {"808", "909", "CYCLES", "CRISP", "LOFI", "MODULAR", "METAL", "WORLD"},
    {
        {50, 52, 48, 18},            /* K1..K4 defaults */
        0,                           /* algo: 808 */
        {0, 80, 100, 62},            /* Amp ADSR (let internal drum envelopes shape hits) */
        {50, 40, 75, 35},            /* Mod Env */
        {0, 45, 0, 0},               /* LFO */
        {0, 3, 1, 70},               /* Arp */
        {0, 50, 18}                  /* Mix: lower default reverb send for tight drums! */
    },
    0.58f,
    kit_block, kit_note, kit_render, 0, kit_draw, 0
};
