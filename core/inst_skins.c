/* A22 - SKINS: two recorded drum kits, STUDIO and ORCH, each in four treatments.
 *
 * A drum on every note of the octave, in KIT's layout: C kick, C# rim, D snare, D# clap (ORCH: a second
 * snare), E F G toms (rising about 4 semitones a step: one recorded tom plays two), F# hat, A# open hat
 * (choked by F#; ORCH: the short clash closes the long one), B ride or cymbal, G# and A two extras. The octaves
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
 * The picture is a ghost in a shell (below, before the InstDef). */
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
     {VZ_TOMLO, 0, 34}, {VZ_TOMHI, -3, 12}, {VZ_VHAT, 0, -30}, {VZ_TOMHI, 1, -14},
     {VZ_TAMB, 0, -42}, {VZ_COWBELL, 0, 22}, {VZ_OHAT, 0, -30}, {VZ_RIDE, 0, 36}},
    /* ORCH: a marching and orchestral kit */
    {{VZ_BASSDRUM, 0, -8}, {VZ_MRIM, 0, 0}, {VZ_MSNARE, 0, 0}, {VZ_SNARE, 0, 14},
     {VZ_TENOR, 0, 30}, {VZ_TENORH, 0, 6}, {VZ_CLASHS, 0, 24}, {VZ_TENORH, 4, -18},
     {VZ_TRIANGLE, 0, 44}, {VZ_TIMP, 0, -36}, {VZ_CLASH, 0, 24}, {VZ_CYMBAL, 0, -26}},
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
 * crush's held sample and count, s[14] which drum, s[15] its semitones; ph[0..1] the reflections' high-pass. */
static void sk_note(Voice *v, const Blk *b)
{
    int type = v->note % 12, i;
    const Pad *p = &KITS[(S.algo >> 2) & 1][type];
    uint32_t a0 = (uint32_t)plunk_zones[p->zone].idx0 << 16;
    (void)b;
    for (i = 0; i < 16; i++) v->s[i] = 0.0f;
    v->ph[0] = v->ph[1] = 0.0f;
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
    f32 kh = onepole_k(1.0f / (2.0f * H1_PI * 200.0f), b->sr);   /* (the reflections' low end taken out: a
                                                                  * kick's 80 Hz would cancel against them) */
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
            f32 rl = 0.62f * e1 + 0.22f * e2, rr = 0.22f * e1 + 0.50f * e2;
            v->ph[0] += (rl - v->ph[0]) * kh;
            v->ph[1] += (rr - v->ph[1]) * kh;
            v->s[10] += (rl - v->ph[0] - v->s[10]) * kr;
            v->s[11] += (rr - v->ph[1] - v->s[11]) * kr;
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

/* ---- the picture: a ghost in a shell ---------------------------------------------------------
 * Three layers. In the middle the ghost, the low end: a soft glow that swells on the kick and breathes.
 * Round it the skin, a membrane for the snare, rim, clap and toms: a strike pushes a dent into it at the
 * angle the drum's pan gives it, and the dent springs back. Outside, the shell, a ring of metal segments
 * for the hats and cymbals: struck, they shimmer; the open hat pushes them apart and the closed hat snaps
 * them shut. ORCH's layers are faceted (octagons), STUDIO's round. LIVE and HALL send ghost shells drifting
 * out (the room); TIGHT and MARCH pull the layers in close, with short, sharp dents; VINTAGE and SHELLAC
 * are gold, drawn with few segments, a little broken. TUNE spaces the layers, DECAY is how slowly a dent
 * springs back, TONE how bright and thin the shell is, ROOM how many ghost shells and how bright. */
#define CX 120.0f
#define CY 108.0f
static f32 sk_e[NDRUM], sk_age[NDRUM];
static uint32_t sk_seen[NVOICE];
static int sk_gold;

static void sk_col(int c) { g_color(sk_gold ? GC_GOLD : c); }
static int sk_metal(int kit, int d)              /* the shell's drums: hats, cymbals and the bells */
{
    return d == HAT || d == OHAT || d == CYM || (d == EXTRA1 && kit) || (d == EXTRA2 && !kit);
}
static f32 sk_angle(int kit, int d) { return (f32)KITS[kit][d].pan * 0.0105f; }   /* turns, 0 at the top */
static f32 sk_facet(int kit, f32 th)             /* ORCH: a radius that draws an octagon, STUDIO: a circle */
{
    f32 u;
    if (!kit) return 1.0f;
    u = th * 8.0f;
    u = (u - (f32)ifloor(u)) - 0.5f;             /* -0.5 .. 0.5 across a facet */
    return 0.924f / cos_turns(u * 0.125f);
}
static f32 sk_wrap(f32 d) { return d - (f32)ifloor(d + 0.5f); }   /* an angle difference, -0.5 .. 0.5 turns */

static void skins_draw(const Vis *vis)
{
    int kit = (vis->algo >> 2) & 1, treat = vis->algo & 3, i, j, n;
    f32 dt = vis->dt, tune = vis->k[0], dec = vis->k[1], tone = vis->k[2], room = vis->k[3];
    f32 gap = (15.0f + 12.0f * tune) * (treat == 2 ? 0.62f : 1.0f);
    f32 r_skin = 23.0f + gap, r_shell = r_skin + gap;
    f32 life = (0.18f + 0.9f * dec) * (treat == 2 ? 0.5f : 1.0f);
    f32 busy = 0.0f, eo, ec, xy[2 * 49];

    sk_gold = treat == 3;
    n = sk_gold ? 12 : 48;
    if (vis->fresh) {
        for (i = 0; i < NDRUM; i++) sk_e[i] = 0.0f, sk_age[i] = 9.0f;
        for (i = 0; i < NVOICE; i++) sk_seen[i] = vis->v[i].serial;
    }
    for (i = 0; i < NVOICE; i++)
        if (vis->v[i].on && vis->v[i].serial != sk_seen[i]) {
            int d = vis->v[i].note % 12;
            sk_seen[i] = vis->v[i].serial;
            sk_e[d] = 0.45f + 0.55f * (f32)vis->v[i].vel * (1.0f / 127.0f);
            sk_age[d] = 0.0f;
            if (d == HAT) sk_e[OHAT] = minf(sk_e[OHAT], 0.05f);       /* (closed: it cuts the open one) */
        }
    for (i = 0; i < NDRUM; i++) {
        sk_age[i] += dt;
        sk_e[i] = maxf(0.0f, sk_e[i] - dt / life);
        busy += sk_e[i];
    }
    eo = sk_e[OHAT];
    ec = sk_e[HAT];

    /* the room: ghost shells drifting out, as many and as bright as ROOM (more on LIVE and HALL) */
    if (room > 0.02f) {
        int ghosts = 1 + (int)(room * 2.5f) + (treat == 1);
        for (j = 0; j < ghosts; j++) {
            f32 ph = vis->t * 0.22f + (f32)j / (f32)ghosts, u = ph - (f32)ifloor(ph);
            f32 r = r_shell + 4.0f + u * (30.0f + 10.0f * room);
            for (i = 0; i <= n; i++) {
                f32 th = (f32)i / (f32)n;
                xy[2 * i] = CX + r * sk_facet(kit, th) * sin_turns(th);
                xy[2 * i + 1] = CY - r * sk_facet(kit, th) * cos_turns(th);
            }
            sk_col(GC_CYAN);
            g_poly(xy, n + 1, 0, 1.0f, room * (1.0f - u) * (0.18f + 0.25f * minf(1.0f, busy)));
        }
    }

    /* the ghost: the low end, swelling on the kick */
    {
        f32 k = sk_e[KICK], breath = 0.5f + 0.5f * sin_turns(vis->t * 0.25f);
        f32 r = (8.0f + 3.0f * breath) * (1.0f + 0.9f * k);
        sk_col(GC_ORANGE);
        g_glow(CX, CY, r_skin * (0.9f + 0.5f * k), 0.16f + 0.40f * k + 0.06f * minf(1.0f, busy));
        if (sk_gold) {
            for (i = 0; i <= 6; i++) {
                f32 th = (f32)i / 6.0f + vis->t * 0.02f;
                xy[2 * i] = CX + r * sin_turns(th);
                xy[2 * i + 1] = CY - r * cos_turns(th);
            }
            g_poly(xy, 7, 0, 1.8f, 0.8f);
        } else {
            g_disc(CX, CY, r * 0.55f, 0.35f + 0.5f * k);
            g_ring(CX, CY, r, 1.8f, 0.6f + 0.4f * k);
        }
    }

    /* the skin: dented where a drum is struck, springing back */
    for (i = 0; i <= n; i++) {
        f32 th = (f32)i / (f32)n, r = r_skin;
        for (j = 0; j < NDRUM; j++) {
            f32 x, b;
            if (j == KICK || sk_metal(kit, j) || sk_e[j] < 0.01f) continue;
            x = sk_wrap(th - sk_angle(kit, j)) * (1.0f / 0.07f);
            if (x <= -1.0f || x >= 1.0f) continue;
            b = (1.0f - x * x) * (1.0f - x * x);
            r -= b * sk_e[j] * (treat == 2 ? 11.0f : 8.0f) * cos_turns(sk_age[j] * (treat == 2 ? 5.0f : 2.6f));
        }
        r *= sk_facet(kit, th);
        xy[2 * i] = CX + r * sin_turns(th);
        xy[2 * i + 1] = CY - r * cos_turns(th);
    }
    sk_col(GC_GREEN);
    g_poly(xy, n + 1, 0, 2.2f, 0.85f);
    for (j = 0; j < NDRUM; j++) {                                    /* where each skin drum sits, a tick */
        f32 th = sk_angle(kit, j), r = r_skin * sk_facet(kit, th);
        if (j == KICK || sk_metal(kit, j)) continue;
        sk_col(j == SNARE || j == RIM ? GC_WHITE : GC_GREEN);
        g_disc(CX + (r - 5.0f) * sin_turns(th), CY - (r - 5.0f) * cos_turns(th), 1.6f + 2.0f * sk_e[j], 0.35f + 0.65f * sk_e[j]);
    }

    /* the shell: metal segments, shimmering where struck, apart for the open hat, shut by the closed one */
    {
        int segs = sk_gold ? 8 : 16;
        f32 spread = 7.0f * eo * (1.0f - ec) - 2.0f * ec;
        for (i = 0; i < segs; i++) {
            f32 c = ((f32)i + 0.5f) / (f32)segs, half = (0.5f / (f32)segs) * (0.72f - 0.25f * eo * (1.0f - ec));
            f32 hit = 0.0f, r;
            int k;
            if (sk_gold && (i % 5) == 3) continue;                   /* (a little broken) */
            for (j = 0; j < NDRUM; j++) {
                f32 x;
                if (!sk_metal(kit, j) || sk_e[j] < 0.01f) continue;
                x = sk_wrap(c - sk_angle(kit, j)) * (1.0f / 0.16f);
                if (x > -1.0f && x < 1.0f) hit = maxf(hit, sk_e[j] * (1.0f - x * x));
            }
            r = r_shell + spread + hit * 3.0f * sin_turns(vis->t * 9.0f + (f32)i * 0.37f);
            for (k = 0; k <= 4; k++) {
                f32 th = c - half + 2.0f * half * (f32)k * 0.25f, rr = r * sk_facet(kit, th);
                xy[2 * k] = CX + rr * sin_turns(th);
                xy[2 * k + 1] = CY - rr * cos_turns(th);
            }
            sk_col(hit > 0.05f ? GC_GOLD : GC_CYAN);
            g_poly(xy, 5, 0, (2.6f - 1.2f * tone) + 1.2f * hit, (0.35f + 0.4f * tone) + 0.5f * hit);
        }
    }
    g_color(GC_WHITE);
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
    sk_block, sk_note, sk_render, 0, skins_draw, 0
};
