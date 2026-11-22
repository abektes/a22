/* A22 - SKINS: two recorded drum kits, STUDIO and ORCH, each in four treatments.
 *
 * A drum on every note of the octave, in KIT's layout: C kick, C# rim, D snare, D# clap (ORCH: a second
 * snare), E F G toms, F# hat, A# open hat (choked by F#), B ride or cymbal, G# and A two extras. The octaves
 * above and below shift the pitch. Every hit is one recording from VCSL (CC0), cut by tools/samples/drums.py
 * into plunk_data.c with KIT's, and read here as KIT reads its own: 4-bit ADPCM at the rate it was stored,
 * its own dying away put back by DECAY. Nothing is synthesized but the treatment around it:
 *   STUDIO, ORCH       dry, as recorded
 *   LIVE, HALL         a longer ring and two early reflections, read from the hit itself a little later:
 *                      two more readers of the same ADPCM, so no delay line and no RAM
 *   TIGHT, MARCH       shorter, gated, with a harder start
 *   VINTAGE, SHELLAC   fewer samples and fewer steps, darker, driven
 *
 *   K1 (BLUE)   TUNE    the kit's pitch, -12 .. +12 semitones
 *   K2 (GREEN)  DECAY   shorter .. longer than recorded
 *   K3 (WHITE)  TONE    dark .. bright
 *   K4 (ORANGE) ROOM    the reflections' level (at 0 the two extra readers do not run)
 *
 * The picture is KIT's pads. */
#include "internal.h"
#include "dsp.h"
#include "plunk_data.h"

enum { KICK, RIM, SNARE, CLAP, TOML, TOMM, HAT, TOMH, EXTRA1, EXTRA2, OHAT, CYM, NDRUM };

typedef struct Pad {
    uint8_t zone;                 /* plunk_zones */
    int8_t semi;                  /* semitones from how it was recorded (one tom played as two) */
    int8_t pan;                   /* hundredths, from the player's seat */
} Pad;

static const Pad KITS[2][NDRUM] = {
    /* STUDIO: an acoustic drum set */
    {{VZ_KICK, 0, 0}, {VZ_XSTICK, 0, 6}, {VZ_SNARE2, 0, 4}, {VZ_CLAP, 0, -8},
     {VZ_TOMLO, -2, 34}, {VZ_TOMHI, -3, 12}, {VZ_HAT, 0, -30}, {VZ_TOMHI, 1, -14},
     {VZ_TAMB, 0, -42}, {VZ_COWBELL, 0, 22}, {VZ_OHAT, 0, -30}, {VZ_RIDE, 0, 36}},
    /* ORCH: a marching and orchestral kit */
    {{VZ_BASSDRUM, 0, -8}, {VZ_MRIM, 0, 0}, {VZ_MSNARE, 0, 0}, {VZ_SNARE, 0, 14},
     {VZ_TIMP, 0, 38}, {VZ_TENOR, 0, -18}, {VZ_CLASHS, 0, 24}, {VZ_TENORH, 0, -32},
     {VZ_TRIANGLE, 0, 44}, {VZ_WOODBLOCK, 0, -40}, {VZ_CLASH, 0, 24}, {VZ_CYMBAL, 0, -26}},
};

typedef struct Treat {
    f32 dec;                      /* times each hit's own dying away */
    f32 punch;                    /* extra level at the start, gone in 6 ms */
    f32 gate;                     /* seconds (times DECAY) after which a hit falls away fast; 0: never */
    f32 crush;                    /* 0..1: how many samples are held over */
    f32 steps;                    /* levels a sample is rounded to; 0: not rounded */
    f32 top;                      /* Hz: the low-pass TONE opens up to */
    f32 drive;
    f32 size;                     /* the reflections' delays, times 9.7 and 17.3 ms */
} Treat;

static const Treat TREAT[4] = {
    /* dry     */ {1.00f, 0.00f, 0.00f, 0.00f,  0.0f, 18000.0f, 1.00f, 1.00f},
    /* room    */ {1.40f, 0.00f, 0.00f, 0.00f,  0.0f, 16000.0f, 1.00f, 1.00f},
    /* tight   */ {0.55f, 0.90f, 0.12f, 0.00f,  0.0f, 18000.0f, 1.40f, 0.70f},
    /* vintage */ {0.90f, 0.20f, 0.00f, 0.55f, 48.0f,  6500.0f, 2.20f, 0.80f},
};

static struct {
    int algo;
    f32 tune, dec, tone, room;
} S;
static uint32_t sk_hat_gen;
static f32 sk_t_was;

static void sk_block(const Blk *b)
{
    if (b->time < sk_t_was) sk_hat_gen = 0;
    sk_t_was = b->time;
    S.algo = b->algo & 7;
    S.tune = (b->k[0] - 0.5f) * 24.0f;
    S.dec = exp2fx((b->k[1] - 0.5f) * 4.0f);
    S.tone = b->k[2];
    S.room = b->k[3];
}

/* ---- an ADPCM reader, packed into two words of a voice and two floats ----
 *   a: the sample it reads next (16 bits) | its step << 16;  b: its prediction (16 bits) | fraction << 16 */
typedef struct Rd {
    uint32_t idx, step, frac;
    int32_t pred;
    f32 prev, cur;
} Rd;

static void rd_load(Rd *r, uint32_t a, uint32_t b, const f32 *pc)
{
    r->idx = a & 0xFFFFu;
    r->step = (a >> 16) & 0x7Fu;
    if (r->step > 88u) r->step = 88u;
    r->pred = (int32_t)(int16_t)(uint16_t)(b & 0xFFFFu);
    r->frac = b >> 16;
    r->prev = pc[0];
    r->cur = pc[1];
}

static void rd_store(const Rd *r, uint32_t *a, uint32_t *b, f32 *pc)
{
    *a = (r->idx & 0xFFFFu) | (r->step << 16);
    *b = ((uint32_t)r->pred & 0xFFFFu) | (r->frac << 16);
    pc[0] = r->prev;
    pc[1] = r->cur;
}

static f32 rd_tick(Rd *r, const PlunkZone *z, const uint8_t *zd, uint32_t inc)
{
    r->frac += inc;
    while (r->frac >= 65536u) {
        r->frac -= 65536u;
        r->prev = r->cur;
        if (r->idx >= z->len && z->loop < z->len) {
            r->idx = z->loop;
            r->pred = z->lpred;
            r->step = z->lidx > 88u ? 88u : z->lidx;
        }
        if (r->idx < z->len) {
            uint32_t nib = (zd[r->idx >> 1] >> ((r->idx & 1u) << 2)) & 15u, sz = plunk_steps[r->step];
            int32_t q = (int32_t)(sz >> 3);
            if (nib & 4u) q += (int32_t)sz;
            if (nib & 2u) q += (int32_t)(sz >> 1);
            if (nib & 1u) q += (int32_t)(sz >> 2);
            r->pred = iclamp(nib & 8u ? r->pred - q : r->pred + q, -32768, 32767);
            r->step = (uint32_t)iclamp((int32_t)r->step + ((nib & 4u) ? (int32_t)((nib & 3u) * 2u + 2u) : -1), 0, 88);
            r->idx++;
            r->cur = (f32)r->pred * (1.0f / 32768.0f);
        } else {
            r->cur = 0.0f;
        }
    }
    return r->prev + (r->cur - r->prev) * ((f32)r->frac * (1.0f / 65536.0f));
}

/* Voice state: u[0..1] the hit's reader, u[2..3] and q[0..1] the two reflections' readers, q[2] the zone
 * | the hat's count << 8, q[3] samples since the note. s[0..5] the readers' last two samples, s[6] the
 * hit's level, s[7..8] TONE's filter, s[9] its presence, s[10..11] the reflections' filter, s[12..13] the
 * crush's held sample and count, s[14] which drum, s[15] its semitones. */
static void sk_note(Voice *v, const Blk *b)
{
    int type = v->note % 12, i;
    const Pad *p = &KITS[(S.algo >> 2) & 1][type];
    uint32_t a0 = (uint32_t)plunk_zones[p->zone].idx0 << 16;
    (void)b;
    for (i = 0; i < 16; i++) v->s[i] = 0.0f;
    v->u[0] = v->u[2] = v->q[0] = a0;
    v->u[1] = v->u[3] = v->q[1] = 0;
    if (type == HAT) sk_hat_gen++;
    v->q[2] = (uint32_t)p->zone | ((sk_hat_gen & 0xFFFFFFu) << 8);
    v->q[3] = 0;
    v->s[6] = 1.0f;
    v->s[13] = 1.0f;
    v->s[14] = (f32)type;
    v->s[15] = (f32)p->semi;
    v->pan = (f32)p->pan * 0.01f;
}

static void sk_render(Voice *v, const Blk *b, f32 *out_l, f32 *out_r, int n)
{
    int type = iclamp((int)v->s[14], 0, NDRUM - 1), i;
    int kit = (S.algo >> 2) & 1;
    const Treat *tr = &TREAT[S.algo & 3];
    uint32_t zone = v->q[2] & 0xFFu, cnt = v->q[3];
    const PlunkZone *z = &plunk_zones[zone < VZ_N ? zone : 0];
    const uint8_t *zd = plunk_data + z->off;
    f32 oct = ((f32)v->note - 60.0f - (f32)type) * (1.0f / 12.0f);
    f32 semis = S.tune + v->s[15] + oct * 6.0f + b->bend;
    f32 vel = lerpf(1.0f, 0.35f + 0.85f * v->vel_f, b->vel_sens);
    f32 rate = exp2fx(clampf(semis * (1.0f / 12.0f), -2.5f, 2.5f));
    uint32_t inc = (uint32_t)(rate * (f32)z->rate * b->isr * 65536.0f);
    f32 kd = 1.0f - onepole_k(maxf(0.008f, z->tau * S.dec * tr->dec), b->sr);
    f32 kg = 1.0f - onepole_k(0.012f, b->sr);
    uint32_t gate_n = tr->gate > 0.0f ? (uint32_t)(tr->gate * S.dec * b->sr) : 0xFFFFFFFFu;
    uint32_t pn = (uint32_t)(0.006f * b->sr);
    f32 gain = z->gain * vel;
    f32 room = S.room * S.room * 0.9f;
    f32 size = tr->size * (kit ? 1.7f : 1.0f);
    uint32_t d1 = (uint32_t)(0.0097f * size * b->sr), d2 = (uint32_t)(0.0173f * size * b->sr);
    f32 top = S.tone < 0.5f ? tr->top * exp2fx((S.tone - 0.5f) * 9.0f) : tr->top;
    f32 pres = S.tone > 0.5f ? (S.tone - 0.5f) * 2.4f : 0.0f;
    f32 kp = onepole_k(1.0f / (2.0f * H1_PI * 2500.0f), b->sr);
    f32 kr = onepole_k(1.0f / (2.0f * H1_PI * 4200.0f), b->sr);
    f32 comp = 1.0f / (0.60f + 0.40f * tr->drive);
    f32 hstep = 1.0f - tr->crush * 0.85f, hold = v->s[12], hc = v->s[13];
    f32 env = v->s[6], gl, gr;
    Rd m, r1, r2;
    Svf lp;

    if (inc > (6u << 16)) inc = 6u << 16;
    if (type == OHAT && ((v->q[2] >> 8) & 0xFFFFFFu) != (sk_hat_gen & 0xFFFFFFu))
        kd = minf(kd, 1.0f - onepole_k(0.010f, b->sr));         /* choked by the closed hat */
    if (env < 0.0002f) {
        for (i = 0; i < n; i++) out_l[i] = out_r[i] = 0.0f;
        v->q[3] = cnt + (uint32_t)n;
        return;
    }
    rd_load(&m, v->u[0], v->u[1], &v->s[0]);
    rd_load(&r1, v->u[2], v->u[3], &v->s[2]);
    rd_load(&r2, v->q[0], v->q[1], &v->s[4]);
    svf_set(&lp, clampf(top, 300.0f, 18000.0f), 0.62f, b->isr);
    pan_gains(v->pan, &gl, &gr);

    for (i = 0; i < n; i++) {
        f32 x = rd_tick(&m, z, zd, inc) * gain * env, lo, bp;
        if (tr->punch > 0.0f && cnt < pn)
            x *= 1.0f + tr->punch * (1.0f - (f32)cnt / (f32)pn);
        if (tr->crush > 0.0f) {              /* (held and rounded first: the filter after smooths the steps) */
            hc += hstep;
            if (hc >= 1.0f) {
                hc -= 1.0f;
                hold = tr->steps > 0.0f ? (f32)ifloor(x * tr->steps + 0.5f) / tr->steps : x;
            }
            x = hold;
        }
        x = softclip(x * tr->drive) * comp;
        SVF_TICK(lp, &v->s[7], x, lo, bp);
        x = lo;
        (void)bp;
        if (pres > 0.0f) {
            v->s[9] += (x - v->s[9]) * kp;
            x += (x - v->s[9]) * pres;
        }
        out_l[i] = x * gl;
        out_r[i] = x * gr;
        if (room > 0.0004f) {
            f32 e1 = cnt >= d1 ? rd_tick(&r1, z, zd, inc) * gain * env * room : 0.0f;
            f32 e2 = cnt >= d2 ? rd_tick(&r2, z, zd, inc) * gain * env * room : 0.0f;
            v->s[10] += (0.62f * e1 + 0.22f * e2 - v->s[10]) * kr;
            v->s[11] += (0.22f * e1 + 0.50f * e2 - v->s[11]) * kr;
            out_l[i] += v->s[10];
            out_r[i] += v->s[11];
        }
        env *= cnt >= gate_n ? kg : kd;
        cnt++;
    }

    rd_store(&m, &v->u[0], &v->u[1], &v->s[0]);
    rd_store(&r1, &v->u[2], &v->u[3], &v->s[2]);
    rd_store(&r2, &v->q[0], &v->q[1], &v->s[4]);
    v->s[6] = env; v->s[12] = hold; v->s[13] = hc;
    v->q[3] = cnt;
}

const InstDef inst_skins = {
    "SKINS",
    {238, 241, 245},
    {"TUNE", "DECAY", "TONE", "ROOM"},
    {"STUDIO", "LIVE", "TIGHT", "VINTAGE", "ORCH", "HALL", "MARCH", "SHELLAC"},
    {
        {50, 50, 60, 0},             /* K1..K4 defaults */
        0,                           /* algo: STUDIO */
        {0, 80, 100, 62},            /* Amp ADSR (each hit's own envelope does the shaping, as KIT's) */
        {50, 40, 75, 35},            /* Mod Env */
        {0, 45, 0, 0},               /* LFO */
        {0, 3, 1, 70},               /* Arp */
        {0, 50, 18}                  /* Mix: a low reverb send, as KIT's */
    },
    2.30f,                           /* (with algo_gain in synth.c: as loud as KIT on a bar of beats) */
    sk_block, sk_note, sk_render, 0, kit_draw, 0
};
