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
 * The picture is the kit itself, from above, the player at the bottom (each drum where its pan puts it):
 * STUDIO's set (kick, snare, hats, two rack toms, the floor tom, the ride) or ORCH's (concert bass drum,
 * marching and rope snares, three tenors, timpani, suspended and clash cymbals, triangle). Heads ripple when
 * struck, cymbals wobble, the hat's plates close when the closed hat cuts the open one. TUNE sizes the heads,
 * DECAY how long they ring, TONE how bright a strike flashes, ROOM the walls and two fainter ripples off them
 * (the two reflections the sound has); TIGHT's ripples are short, VINTAGE and SHELLAC are in gold, coarser. */
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
     {VZ_TOMLO, -2, 34}, {VZ_TOMHI, 0, 12}, {VZ_HAT, 0, -30}, {VZ_TOMHI, 4, -14},
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

/* ---- the picture ------------------------------------------------------------------------- */
enum { P_NONE, P_KICK, P_BASS, P_DRUM, P_SNARE, P_RIM, P_HAT, P_CYM, P_CLASH, P_TIMP, P_TRI, P_TAMB, P_COW, P_CLAP };
typedef struct Piece { uint8_t kind, x, y, r; } Piece;

static const Piece PIECE[2][NDRUM] = {
    /* STUDIO:  kick, rim (on the snare), snare, clap, floor tom, rack tom 2, hats, rack tom 1, tambourine,
     *          cowbell, open hats (the hats' piece), ride */
    {{P_KICK, 120, 112, 22}, {P_RIM, 96, 146, 15}, {P_SNARE, 96, 146, 15}, {P_CLAP, 66, 164, 7},
     {P_DRUM, 164, 142, 18}, {P_DRUM, 140, 78, 13}, {P_HAT, 56, 126, 17}, {P_DRUM, 102, 78, 12},
     {P_TAMB, 30, 160, 8}, {P_COW, 160, 106, 7}, {P_NONE, 56, 126, 17}, {P_CYM, 192, 84, 24}},
    /* ORCH:    bass drum, rim (on the marching snare), marching snare, rope snare, three tenors, the clash
     *          cymbals held short (the pair's piece), triangle, timpani, let ring (the pair), suspended */
    {{P_BASS, 116, 106, 27}, {P_RIM, 98, 152, 13}, {P_SNARE, 98, 152, 13}, {P_SNARE, 140, 152, 13},
     {P_DRUM, 152, 56, 12}, {P_DRUM, 126, 56, 11}, {P_CLASH, 190, 116, 17}, {P_DRUM, 100, 56, 10},
     {P_TRI, 198, 54, 10}, {P_TIMP, 46, 134, 22}, {P_NONE, 190, 116, 17}, {P_CYM, 52, 72, 20}},
};

static f32 sk_e[NDRUM], sk_age[NDRUM];
static uint32_t sk_seen[NVOICE];
static int sk_gold;

static f32 sn(f32 x) { return sk_gold ? (f32)ifloor(x * (1.0f / 3.0f) + 0.5f) * 3.0f : x; }   /* (VINTAGE: coarser) */
static void sk_col(int c) { g_color(sk_gold ? GC_GOLD : c); }

/* rings rippling out over a head of radius r, and ROOM's two fainter ones a moment later */
static void sk_ripple(f32 x, f32 y, f32 r, int d, f32 speed, f32 room, int c)
{
    static const f32 LAG[2] = {0.06f, 0.11f};
    f32 e = sk_e[d], age = sk_age[d];
    int j;
    if (e < 0.02f) return;
    sk_col(c);
    for (j = 0; j < 2; j++) {
        f32 u = age * speed - 0.45f * (f32)j;
        if (u > 0.0f && u < 1.0f) g_ring(sn(x), sn(y), r * (0.2f + 0.8f * u), 1.6f, e * (1.0f - u));
    }
    if (room > 0.02f)
        for (j = 0; j < 2; j++) {
            f32 u = (age - LAG[j]) * speed * 0.7f;
            if (u > 0.0f && u < 1.0f) g_ring(sn(x), sn(y), r * (1.0f + 0.9f * u), 1.0f, room * e * 0.45f * (1.0f - u));
        }
}

static void skins_draw(const Vis *vis)
{
    int kit = (vis->algo >> 2) & 1, treat = vis->algo & 3, i;
    f32 dt = vis->dt, tune = vis->k[0], dec = vis->k[1], tone = vis->k[2], room = vis->k[3];
    f32 sz = 0.86f + 0.28f * tune, speed = treat == 2 ? 4.2f : 2.2f;
    const Piece *P = PIECE[kit];

    sk_gold = treat == 3;
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
        sk_e[i] = maxf(0.0f, sk_e[i] - dt / ((0.10f + 0.9f * dec) * (treat == 2 ? 0.5f : 1.0f) * (treat == 1 ? 1.4f : 1.0f)));
    }

    /* the room: its walls as ROOM brings them in */
    if (room > 0.02f) {
        sk_col(GC_SLATE);
        g_box(sn(16.0f), sn(38.0f), 208.0f, 140.0f, 1.0f, 0.10f + 0.40f * room);
    }

    for (i = 0; i < NDRUM; i++) {
        const Piece *p = &P[i];
        f32 x = (f32)p->x, y = (f32)p->y, r = (f32)p->r * sz, e = sk_e[i];
        f32 flash = e * (0.20f + 0.55f * tone);
        switch (p->kind) {
        case P_KICK: {                                               /* a bass drum on its side: shell, two heads */
            f32 h = r * 1.25f;
            sk_col(GC_ORANGE);
            g_box(sn(x - r), sn(y - h * 0.5f), 2.0f * r, h, 1.8f, 0.8f);
            g_ellipse(sn(x), sn(y + h * 0.5f), r, 3.0f + 5.0f * e, 1.6f, 0.7f + 0.3f * e);   /* the beater's head */
            g_ellipse(sn(x), sn(y - h * 0.5f), r, 3.0f, 1.2f, 0.5f);
            if (e > 0.02f) { g_rect(sn(x - r), sn(y - h * 0.5f), 2.0f * r, h, flash * 0.5f); }
            sk_ripple(x, y, r, i, speed, room, GC_ORANGE);
            break;
        }
        case P_BASS:                                                 /* a concert bass drum in its frame */
            sk_col(GC_ORANGE);
            g_ring(sn(x), sn(y), r, 2.2f, 0.85f);
            g_ring(sn(x), sn(y), r * 0.86f, 1.0f, 0.4f);
            g_line(sn(x - r * 0.7f), sn(y + r * 0.7f), sn(x - r * 0.9f), sn(y + r * 1.25f), 1.4f, 0.6f);
            g_line(sn(x + r * 0.7f), sn(y + r * 0.7f), sn(x + r * 0.9f), sn(y + r * 1.25f), 1.4f, 0.6f);
            if (e > 0.02f) g_disc(sn(x), sn(y), r * 0.86f, flash * 0.6f);
            sk_ripple(x, y, r * 0.86f, i, speed * 0.8f, room, GC_ORANGE);
            break;
        case P_DRUM:
        case P_SNARE: {
            int c = p->kind == P_SNARE ? GC_WHITE : (kit ? GC_BLUE : GC_GREEN);
            sk_col(c);
            g_ring(sn(x), sn(y), r, 2.0f, 0.85f);
            if (p->kind == P_SNARE) {                                /* the wires under the head */
                g_line(sn(x - r * 0.6f), sn(y - 2.5f), sn(x + r * 0.6f), sn(y - 2.5f), 1.0f, 0.35f + 0.4f * e);
                g_line(sn(x - r * 0.6f), sn(y + 2.5f), sn(x + r * 0.6f), sn(y + 2.5f), 1.0f, 0.35f + 0.4f * e);
            }
            if (e > 0.02f) g_disc(sn(x), sn(y), r * 0.9f, flash);
            sk_ripple(x, y, r, i, speed, room, c);
            break;
        }
        case P_RIM: {                                                /* the stick across the snare's rim */
            f32 a = sk_e[i];
            if (a > 0.02f) {
                sk_col(GC_CYAN);
                g_line(sn(x - r * 1.3f), sn(y + r * 0.9f), sn(x + r * 0.5f), sn(y - r * 0.9f), 2.2f, a);
                g_arc(sn(x), sn(y), r, 0.55f, 0.95f, 2.6f, a);
            }
            break;
        }
        case P_HAT: {                                                /* two plates: apart while open, shut on the closed hit */
            f32 eo = sk_e[OHAT], ec = sk_e[HAT];
            f32 gap = 1.5f + 9.0f * eo * (1.0f - ec);
            sk_col(GC_GOLD);
            g_line(sn(x), sn(y - gap * 0.5f - 4.0f), sn(x), sn(y + 16.0f), 1.2f, 0.5f);
            g_ellipse(sn(x), sn(y - gap * 0.5f), r, r * 0.28f, 1.6f, 0.75f + 0.25f * maxf(eo, ec));
            g_ellipse(sn(x), sn(y + gap * 0.5f), r, r * 0.28f, 1.6f, 0.65f + 0.35f * maxf(eo, ec));
            if (maxf(eo, ec) > 0.02f) g_ellipse(sn(x), sn(y), r, r * 0.28f + gap * 0.5f, 0.0f, maxf(eo, ec) * (0.15f + 0.3f * tone));
            break;
        }
        case P_CYM: {                                                /* on its stand, wobbling when struck */
            f32 ry = r * 0.32f * (1.0f + 0.7f * e * sin_turns(sk_age[i] * 4.5f));
            sk_col(GC_GOLD);
            g_line(sn(x), sn(y), sn(x), sn(y + 26.0f), 1.2f, 0.5f);
            g_ellipse(sn(x), sn(y), r, maxf(1.5f, absf(ry)), 1.8f, 0.8f + 0.2f * e);
            g_disc(sn(x), sn(y), 2.5f, 0.9f);
            if (e > 0.02f) g_ellipse(sn(x), sn(y), r, maxf(1.5f, absf(ry)), 0.0f, flash * 0.5f);
            if (room > 0.02f && e > 0.02f) g_ellipse(sn(x), sn(y), r * (1.0f + 0.5f * (1.0f - e)), maxf(1.5f, absf(ry)) * 1.4f, 1.0f, room * e * 0.35f);
            break;
        }
        case P_CLASH: {                                              /* a pair: swung apart while ringing, held shut short */
            f32 eo = sk_e[OHAT], ec = sk_e[HAT];
            f32 gap = 2.0f + 12.0f * eo * (1.0f - ec);
            f32 wob = 1.0f + 0.3f * eo * sin_turns(sk_age[OHAT] * 4.0f);
            sk_col(GC_GOLD);
            g_ellipse(sn(x - gap * 0.5f), sn(y), r * 0.3f * wob, r, 1.8f, 0.8f + 0.2f * maxf(eo, ec));
            g_ellipse(sn(x + gap * 0.5f), sn(y), r * 0.3f * wob, r, 1.8f, 0.8f + 0.2f * maxf(eo, ec));
            if (maxf(eo, ec) > 0.02f) g_glow(sn(x), sn(y), r, maxf(eo, ec) * (0.2f + 0.3f * tone));
            break;
        }
        case P_TIMP:                                                 /* a copper bowl on three legs */
            sk_col(GC_ORANGE);
            g_ring(sn(x), sn(y), r, 2.4f, 0.85f);
            g_ring(sn(x), sn(y), r * 0.72f, 1.0f, 0.35f);
            g_line(sn(x - r * 0.8f), sn(y + r * 0.6f), sn(x - r), sn(y + r * 1.15f), 1.3f, 0.5f);
            g_line(sn(x + r * 0.8f), sn(y + r * 0.6f), sn(x + r), sn(y + r * 1.15f), 1.3f, 0.5f);
            if (e > 0.02f) g_disc(sn(x), sn(y), r * 0.9f, flash * 0.7f);
            sk_ripple(x, y, r, i, speed * 0.7f, room, GC_ORANGE);
            break;
        case P_TRI: {
            f32 t[6] = {sn(x), sn(y - r), sn(x + r * 0.9f), sn(y + r * 0.7f), sn(x - r * 0.9f), sn(y + r * 0.7f)};
            sk_col(GC_CYAN);
            g_poly(t, 3, 0, 1.6f + e, 0.7f + 0.3f * e);
            if (e > 0.02f) g_glow(sn(x), sn(y), r * 1.4f, e * 0.4f);
            break;
        }
        case P_TAMB: {
            int j;
            sk_col(GC_GOLD);
            g_ring(sn(x), sn(y), r, 1.6f, 0.7f + 0.3f * e);
            for (j = 0; j < 5; j++) {
                f32 a = (f32)j * 0.2f + e * 0.06f * sin_turns(sk_age[i] * 9.0f);
                g_disc(sn(x + cos_turns(a) * r), sn(y + sin_turns(a) * r), 1.6f, 0.6f + 0.4f * e);
            }
            break;
        }
        case P_COW: {
            f32 cb[8] = {sn(x - r * 0.5f), sn(y - r), sn(x + r * 0.5f), sn(y - r), sn(x + r * 0.9f), sn(y + r), sn(x - r * 0.9f), sn(y + r)};
            sk_col(GC_GOLD);
            g_poly(cb, 4, 1, 1.5f, 0.7f + 0.3f * e);
            if (e > 0.02f) g_fill(cb, 4, flash * 0.6f);
            break;
        }
        case P_CLAP:
            sk_col(GC_WHITE);
            g_arc(sn(x - 3.0f), sn(y), r, 0.30f, 0.70f, 1.6f, 0.5f + 0.5f * e);
            g_arc(sn(x + 3.0f), sn(y), r, 0.80f, 1.20f, 1.6f, 0.5f + 0.5f * e);
            if (e > 0.02f) g_glow(sn(x), sn(y), r * 1.6f, e * 0.35f);
            break;
        default:
            break;
        }
    }
    /* the player's stool */
    sk_col(GC_SLATE);
    g_ring(sn(120.0f), sn(172.0f), 7.0f, 1.2f, 0.45f);
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
