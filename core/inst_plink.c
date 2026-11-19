/* h1 - PLINK: a drum machine that is a pin board. Twelve drums, one on every note of the octave as on
 * BONK (the octave you play it in tunes it), each of them a small synthesised hit. A hit drops something
 * in at the top of the board, in the drum's own chute, and it comes down from pin to pin in time: every
 * landing is on a sixteenth of the beat. The algorithm is the kit, and it changes what falls and what
 * it falls through.
 *
 *   C kick  C# rim  D snare  D# clap  E low tom  F mid tom  F# hat  G high tom
 *   G# shaker  A cowbell  A# open hat  B crash
 *
 *   K1 TUNE    every drum up or down                   what falls shrinks or grows, and the pins with it
 *   K2 DECAY   how long they ring                      the tails behind what falls, how long a pin glows
 *   K3 BOUNCE  the pitch drop, then the hit comes      how high it hops from pin to pin, how hard the
 *              back again and again, like a ball       pins are knocked
 *   K4 BITS    fewer samples, fewer levels             the board falls onto a coarser and coarser grid
 */
#include "internal.h"
#include "dsp.h"

/* ---- sound ------------------------------------------------------------------------------ */
enum { KICK, RIM, SNARE, CLAP, TOML, TOMM, HAT, TOMH, SHAKE, COW, OHAT, CRASH, NDRUM };

typedef struct Drum {
    f32 f0, sweep, tsw, tdec, tone;              /* the tone: Hz, octaves of drop, its time, decay, level */
    f32 nlev, nfc, ndec;                         /* the noise: level, its clock or filter in Hz, decay */
    int8_t deg;                                  /* its note in a tuned kit, semitones over the kit's root */
    uint8_t nmode;                               /* 0 low-pass, 1 band-pass, 2 high-pass */
} Drum;

static const Drum DRUMS[NDRUM] = {
    /* f0    sweep  tsw     tdec   tone  nlev   nfc    ndec   deg mode */
    {55.0f, 2.4f, 0.035f, 0.22f, 1.0f, 0.0f, 2000, 0.010f, 0, 0},       /* kick */
    {880.0f, 0.2f, 0.004f, 0.03f, 0.7f, 0.25f, 6000, 0.010f, 24, 1},    /* rim */
    {196.0f, 0.6f, 0.015f, 0.08f, 0.5f, 0.9f, 5000, 0.13f, 12, 1},      /* snare */
    {392.0f, 0.0f, 0.010f, 0.05f, 0.0f, 1.0f, 1800, 0.15f, 15, 1},      /* clap */
    {98.0f, 0.9f, 0.05f, 0.25f, 1.0f, 0.05f, 900, 0.03f, 3, 0},         /* low tom */
    {147.0f, 0.8f, 0.045f, 0.20f, 1.0f, 0.05f, 1200, 0.03f, 7, 0},      /* mid tom */
    {1568.0f, 0.0f, 0.010f, 0.03f, 0.0f, 0.8f, 11000, 0.035f, 27, 2},   /* hat */
    {220.0f, 0.7f, 0.04f, 0.17f, 1.0f, 0.05f, 1700, 0.03f, 10, 0},      /* high tom */
    {1175.0f, 0.0f, 0.010f, 0.04f, 0.0f, 0.7f, 8000, 0.06f, 22, 1},     /* shaker */
    {587.0f, 0.0f, 0.010f, 0.16f, 0.9f, 0.0f, 2000, 0.01f, 19, 1},      /* cowbell */
    {1760.0f, 0.0f, 0.010f, 0.20f, 0.0f, 0.8f, 11000, 0.30f, 31, 2},    /* open hat */
    {2093.0f, 0.0f, 0.010f, 0.50f, 0.0f, 0.9f, 9000, 0.80f, 34, 2},     /* crash */
};

typedef struct Kit {
    f32 root;                                    /* > 0: a tuned kit. Every drum is a note over this, in Hz */
    f32 pitch, sweep, dec, noise;                /* multipliers */
    f32 fm;                                      /* how far a second sine bends the tone */
    f32 p2r, p2;                                 /* a partial above the tone: its ratio (the bender's too), its level */
    f32 alt;                                     /* how much tone the drums of noise are given */
    uint8_t wave;                                /* 0 sine, 1 a triangle in sixteen steps, 2 pulse, 3 saw */
    uint8_t lfsr;                                /* the noise is a shift register on a clock */
    uint8_t step;                                /* pitch and loudness move sixty times a second, loudness in sixteen steps */
    int8_t updown;                               /* -1: the pitch rises into the note instead */
} Kit;

static const Kit KITS[NALGO] = {
    /* root   pitch  sweep  dec    noise  fm    p2r    p2     alt   wave lfsr step updown */
    {0.0f, 1.0f, 1.0f, 0.8f, 1.0f, 0.0f, 2.0f, 0.0f, 0.0f, 1, 1, 1, 1},          /* 8BIT: a sound chip */
    {0.0f, 0.9f, 0.9f, 1.5f, 0.85f, 0.0f, 1.5f, 0.0f, 0.2f, 0, 0, 0, 1},         /* ANALOG: sines and hiss, long */
    {0.0f, 1.12f, 1.6f, 0.65f, 1.25f, 0.5f, 2.0f, 0.0f, 0.15f, 0, 0, 0, 1},      /* HOUSE: short and hard */
    {0.0f, 1.1f, 1.2f, 1.0f, 0.7f, 2.4f, 1.5f, 0.0f, 0.45f, 0, 0, 0, 1},         /* FM: one sine bends another */
    {0.0f, 2.4f, 3.2f, 0.9f, 0.6f, 0.0f, 2.0f, 0.0f, 0.3f, 3, 1, 0, 1},          /* ZAP: saws that dive */
    {0.0f, 1.6f, 0.4f, 0.3f, 1.1f, 3.5f, 3.37f, 0.3f, 0.6f, 2, 1, 0, -1},        /* GLITCH: never the same twice */
    {523.0f, 1.0f, 0.1f, 0.22f, 0.12f, 0.0f, 2.0f, 0.0f, 1.0f, 0, 0, 0, 1},      /* CLICK: tuned blips, hardly there */
    {0.0f, 0.85f, 2.2f, 1.5f, 1.2f, 1.0f, 1.0f, 0.0f, 0.3f, 0, 0, 0, 1},         /* RAVE: everything through the fuzz */
};

static struct {
    int algo;
    f32 tune, dec, bounce, bits, gap;
} K;
static uint32_t hat_gen;

static void plink_block(const Blk *b)
{
    K.algo = b->algo & 7;
    K.tune = (b->k[0] - 0.5f) * 24.0f;
    K.dec = exp2fx((b->k[1] - 0.5f) * 5.0f);
    K.bounce = b->k[2];
    K.bits = b->k[3];
    K.gap = b->sr * 7.5f / clampf(b->bpm, 30.0f, 300.0f);      /* a thirty-second note, in samples */
}

static void plink_note(Voice *v, const Blk *b)
{
    int type = v->note % 12, i;
    (void)b;
    if (!v->u[0]) v->u[0] = 0x91E4C0DEu + v->idx * 977u;
    v->s[8] = (f32)type;
    v->s[4] = v->s[5] = v->s[7] = 1.0f;          /* the tone's and the noise's loudness, the pitch drop */
    v->s[0] = v->s[1] = 0.0f;
    v->s[6] = maxf(0.0f, K.bounce - 0.3f) * 1.1f;        /* how loud it comes back the next time */
    v->s[9] = v->s[10] = v->s[11] = 0.0f;
    v->s[12] = 0.0f;
    v->s[13] = 1.0f;
    for (i = 0; i < 6; i++)
        v->ph[i] = 0.0f;
    v->u[1] = 0;
    v->q[0] = (uint32_t)K.gap;                   /* when */
    v->q[1] = (uint32_t)K.gap;                   /* and how long after that */
    v->q[2] = 0x4A31u + (uint32_t)type;          /* the shift register */
    v->q[3] = 0;
    if (type == HAT)
        hat_gen++;
    v->u[2] = hat_gen;
    v->pan = clampf(((f32)type - 5.5f) * 0.11f, -0.6f, 0.6f);
}

static void plink_render(Voice *v, const Blk *b, f32 *out, int n)
{
    int type = (int)v->s[8], i;
    const Drum *d = &DRUMS[type >= 0 && type < NDRUM ? type : 0];
    const Kit *k = &KITS[K.algo];
    Svf fn;
    f32 oct = ((f32)v->note - 60.0f - (f32)type) * (1.0f / 12.0f);     /* which octave it was played in */
    f32 semis = K.tune + oct * (k->root > 0.0f ? 12.0f : 5.0f) + b->bend + (K.algo == 5 ? (v->r1 - 0.5f) * 14.0f : 0.0f);
    f32 f0 = (k->root > 0.0f ? k->root * exp2fx((f32)d->deg * (1.0f / 12.0f)) : d->f0 * k->pitch) * exp2fx(semis * (1.0f / 12.0f));
    f32 dec = K.dec * k->dec;
    f32 sweep = d->sweep * k->sweep * (0.35f + 1.3f * K.bounce) * (f32)k->updown;
    f32 kt = 1.0f - onepole_k(d->tdec * dec, b->sr), kn = 1.0f - onepole_k(d->ndec * dec, b->sr);
    f32 ks = 1.0f - onepole_k(d->tsw * (k->updown < 0 ? 3.0f : 1.0f), b->sr);
    f32 at = v->s[4], an = v->s[5], back = v->s[6], sw = v->s[7], ph = v->s[9], ph2 = v->s[10], nacc = v->s[11];
    f32 tone = k->root > 0.0f ? 1.0f : maxf(d->tone, k->alt), nlev = d->nlev * k->noise;
    f32 nclk = d->nfc * (0.8f + 0.4f * v->r2) * exp2fx(semis * (1.0f / 24.0f));
    f32 crush = clampf((k->step ? 0.25f : 0.0f) + 0.85f * K.bits, 0.0f, 0.95f), hstep = 1.0f - crush * 0.93f;
    f32 levels = 2.0f + 46.0f * (1.0f - K.bits) * (1.0f - K.bits), hold = v->s[12], hc = v->s[13];
    f32 duty = type == RIM ? 0.125f : (type == COW ? 0.25f : 0.5f);
    f32 la = v->s[2], lf = v->s[3], nb = 1.0f;
    uint32_t reg = v->q[2] | 1u, tick = v->q[3], per = (uint32_t)(b->sr * (1.0f / 60.0f));
    f32 drive = K.algo == 7 ? 7.0f : 1.4f, dnorm = K.algo == 7 ? 0.75f : 1.0f;
    int wave = k->wave, clap = type == CLAP;

    if (wave == 1 && (type == RIM || type == COW))
        wave = 2;                                /* 8BIT: the two that are not the triangle's */
    if (type == OHAT && v->u[2] != hat_gen)      /* a closed hat chokes the open one */
        kn = kt = 0.995f;
    if (type == SHAKE)
        kn = 1.0f - onepole_k(d->ndec * dec * 0.7f, b->sr);
    svf_set(&fn, k->root > 0.0f && type != HAT && type != OHAT && type != CRASH ? minf(f0 * 1.5f, 9000.0f) : minf(nclk, 14000.0f),
            k->root > 0.0f ? 3.0f : 0.9f, b->isr);
    if (per < 64u)
        per = 64u;

    for (i = 0; i < n; i++) {
        f32 x = 0.0f, y, f, lo, bp, nz;
        if (back > 0.04f && v->u[1] >= v->q[0]) {        /* BOUNCE: it lands again, sooner and softer each time */
            at = an = back;
            sw = 0.5f;
            back *= 0.62f;
            v->q[1] = (v->q[1] * 3u) >> 2;
            if (v->q[1] < 400u)
                back = 0.0f;
            v->q[0] += v->q[1];
            tick = per;
        }
        if (!k->step || ++tick >= per) {         /* 8BIT: the sound chip is told sixty times a second */
            tick = 0;
            la = k->step ? (f32)ifloor(at * 15.0f + 0.5f) * (1.0f / 15.0f) : at;
            lf = minf(f0 * exp2fx(sweep * sw), 11000.0f) * b->isr;
        }
        f = lf;
        if (tone > 0.0f) {
            ph += f;
            if (ph >= 1.0f) ph -= 1.0f;
            ph2 += f * k->p2r;
            if (ph2 >= 1.0f) ph2 -= (f32)(int)ph2;
            switch (wave) {
            case 1: y = ph < 0.5f ? ph * 4.0f - 1.0f : 3.0f - ph * 4.0f; y = (f32)ifloor(y * 7.5f + 0.5f) * (1.0f / 7.5f); break;
            case 2: y = pulse_bl(ph, f, duty) * 0.6f; break;
            case 3: y = saw_bl(ph, f) * 0.6f; break;
            default: y = tsin(ph + k->fm * 0.16f * la * tsin(ph2)); break;
            }
            x = tone * la * (y + k->p2 * la * tsin(ph2));
            at *= kt;
        }
        sw *= ks;
        if (nlev > 0.0f) {
            if (k->lfsr) {                       /* a shift register, moved on by its own clock */
                nacc += nclk * b->isr * (type == CRASH || type == SNARE ? 0.6f : 1.6f);
                if (nacc >= 1.0f) {
                    nacc -= 1.0f;
                    reg = (reg >> 1) | (((reg ^ (reg >> (type == COW || type == RIM ? 6 : 1))) & 1u) << 14);
                    nb = reg & 1u ? 1.0f : -1.0f;
                }
                nz = nb * 0.6f;
            } else {
                nz = noise_s(&v->u[0]);
                SVF_TICK(fn, &v->s[0], nz, lo, bp);
                nz = d->nmode == 0 ? lo : (d->nmode == 1 ? bp * 1.4f : nz - lo - bp * fn.k);
            }
            x += nlev * (k->step ? (f32)ifloor(an * 15.0f + 0.5f) * (1.0f / 15.0f) : an) * nz;
            an *= kn;
            if (clap && (v->u[1] == 420u || v->u[1] == 860u || v->u[1] == 1300u))
                an = 1.0f;
        }
        v->u[1]++;
        x = softclip(x * drive) * dnorm;
        if (crush > 0.01f) {                     /* BITS */
            hc += hstep;
            if (hc >= 1.0f) {
                hc -= 1.0f;
                hold = (f32)ifloor(x * levels + 0.5f) / levels;
            }
            x = hold;
        }
        out[i] = x;
    }
    v->s[2] = la; v->s[3] = lf;
    v->s[4] = at; v->s[5] = an; v->s[6] = back; v->s[7] = sw; v->s[9] = ph; v->s[10] = ph2; v->s[11] = nacc;
    v->s[12] = hold; v->s[13] = hc;
    v->q[2] = reg; v->q[3] = tick;
}

/* ---- picture ---------------------------------------------------------------------------- */
#define ROWS 9
#define COLS 13
#define NBALL 28
#define DX 18.0f
#define DY 19.0f
#define TOP 36.0f
#define FLOOR 214.0f

typedef struct Ball {
    uint8_t on, type, fresh;
    int8_t row;                                  /* the row it is on its way to: 0..ROWS - 1, ROWS = the floor */
    int16_t hx;                                  /* where that is across, in half pin spaces */
    f32 x0, y0, u0;                              /* where this hop began, and how far into the sixteenth */
    f32 spin;
} Ball;
static Ball ball[NBALL];
static f32 peg_e[ROWS][COLS], peg_k[ROWS][COLS];        /* a pin that was hit: its glow, how far it is knocked */
static f32 bin[NDRUM + 1], bin_v[NDRUM + 1];
static uint32_t seen_serial[NVOICE];
static f32 sub_prev;
static const uint8_t SIZE[NDRUM] = {70, 34, 56, 48, 60, 52, 30, 46, 36, 44, 38, 64};

/* is there a pin at (row, half space hx)? The kit's board. */
static int pin(int algo, int row, int hx)
{
    int c = hx >> 1, mid = hx - 13;
    if (row < 0 || row >= ROWS || hx < 1 || hx > 25 || ((hx ^ row) & 1) == 0)
        return 0;
    switch (algo) {
    case 1: return (mid < 0 ? -mid : mid) <= 3 + (row * 3) / 2;              /* ANALOG: a pyramid */
    case 2: return ((c + row * 2) % 3) != 0;                           /* HOUSE: gaps to fall through */
    case 4: return (mid < 0 ? -mid : mid) <= 4 + (row < 4 ? 8 - row * 2 : row * 2 - 8) + 2;   /* ZAP: an hourglass */
    case 5: return row % 3 != 1 || (c & 1);                            /* GLITCH */
    case 6: return ((hx + row * 3) % 8) < 6;                           /* CLICK: slanted lanes */
    case 7: return ((uint32_t)(row * 31 + hx * 17) * 2654435761u >> 28) > 3u;   /* RAVE: pins missing */
    default: return 1;
    }
}

static f32 hx_x(int hx) { return 120.0f + (f32)(hx - 13) * (DX * 0.5f); }
static f32 row_y(int row) { return TOP + (f32)row * DY; }

/* a small figure, s across, turned by r: what falls (big) and what it falls through (small) */
static void glyph(int kind, f32 x, f32 y, f32 s, f32 r, f32 a, int solid)
{
    f32 c = cos_turns(r) * s, sn = sin_turns(r) * s, xy[12];
    int i;
    switch (kind) {
    case 0:                                      /* 8BIT: a block */
        g_rect(x - s, y - s, 2.0f * s, 2.0f * s, a);
        break;
    case 1:                                      /* ANALOG: a dial */
        g_ring(x, y, s, 1.0f + s * 0.16f, a);
        if (solid) g_line(x, y - s * 0.45f, x, y + s * 0.45f, 1.0f + s * 0.2f, a);
        break;
    case 3:                                      /* FM: a ring; its pins are posts */
        if (solid) g_ring(x, y, s * 0.72f, s * 0.56f + 0.6f, a);
        else g_line(x, y - s, x, y + s, 1.4f, a);
        break;
    case 4:                                      /* ZAP: a drop of solder */
        g_disc(x, y + s * 0.2f, s * 0.8f, a);
        if (solid) {
            xy[0] = x - s * 0.62f; xy[1] = y - s * 0.1f;
            xy[2] = x + s * 0.62f; xy[3] = y - s * 0.1f;
            xy[4] = x; xy[5] = y - s * 1.5f;
            g_fill(xy, 3, a);
        }
        break;
    case 5:                                      /* GLITCH: a burr */
        for (i = 0; i < 3; i++) {
            f32 an = r + (f32)i * (1.0f / 6.0f), u = cos_turns(an) * s, w = sin_turns(an) * s;
            g_line(x - u, y - w, x + u, y + w, solid ? 1.0f + s * 0.22f : 1.0f, a);
        }
        break;
    case 6:                                      /* CLICK: a spark */
        xy[0] = x; xy[1] = y - s * 1.3f; xy[2] = x + s * 0.7f; xy[3] = y;
        xy[4] = x; xy[5] = y + s * 1.3f; xy[6] = x - s * 0.7f; xy[7] = y;
        if (solid) g_fill(xy, 4, a);
        else g_poly(xy, 4, 1, 1.0f, a);
        break;
    case 7:                                      /* RAVE: a nut */
        for (i = 0; i < 6; i++) {
            f32 an = r + (f32)i * (1.0f / 6.0f);
            xy[2 * i] = x + cos_turns(an) * s;
            xy[2 * i + 1] = y + sin_turns(an) * s;
        }
        if (solid) {
            g_poly(xy, 6, 1, 1.0f + s * 0.2f, a);
            g_ring(x, y, s * 0.36f, 1.0f, a);
        } else {                                 /* (its pins are screw heads) */
            g_line(xy[0], xy[1], xy[6], xy[7], 1.0f, a);
            g_line(xy[2] * 0.5f + xy[4] * 0.5f, xy[3] * 0.5f + xy[5] * 0.5f, xy[8] * 0.5f + xy[10] * 0.5f, xy[9] * 0.5f + xy[11] * 0.5f, 1.0f, a);
        }
        break;
    default:                                     /* HOUSE: a ball */
        g_disc(x, y, s, a);
        if (solid && s > 3.0f) {
            g_mode(G_PAINT);
            g_disc(x - c * 0.38f + sn * 0.2f, y - sn * 0.38f - c * 0.2f, s * 0.26f, 0.0f);
            g_mode(G_MAX);
        }
        break;
    }
}

/* where a ball is, u of the way through its hop (0..1) */
static void ball_at(const Ball *bl, f32 u, f32 hop, f32 *x, f32 *y)
{
    f32 x1 = hx_x(bl->hx), y1 = bl->row >= ROWS ? FLOOR : row_y(bl->row) - 4.0f;
    f32 w = sat((u - bl->u0) / maxf(1.0f - bl->u0, 0.02f));
    *x = lerpf(bl->x0, x1, w);
    *y = lerpf(bl->y0, y1, w * w) - (bl->fresh ? 0.0f : hop * 4.0f * w * (1.0f - w));
}

static f32 snapf(f32 v, f32 q) { return q > 1.0f ? (f32)ifloor(v / q + 0.5f) * q : v; }

static void plink_draw(const Vis *vis)
{
    f32 dt = vis->dt, tune = vis->k[0], decay = vis->k[1], bounce = vis->k[2], bits = vis->k[3];
    f32 scale = 1.3f - 0.6f * tune, hop = 3.0f + 15.0f * bounce, q = 1.0f + (f32)ifloor(bits * 7.0f + 0.5f);
    f32 sub = fractf(vis->beat * 4.0f), glow_k = 1.0f / (0.1f + 0.8f * decay * decay);
    f32 pulse = (1.0f - vis->beat) * (1.0f - vis->beat);
    int algo = vis->algo & 7, i, r, c, landed = sub < sub_prev, tails = 1 + (int)(decay * 3.4f);

    if (algo == 0 && q < 3.0f)
        q = 3.0f;                                /* (8BIT is never smooth) */
    if (vis->fresh) {
        for (i = 0; i < NBALL; i++)
            ball[i].on = 0;
        for (r = 0; r < ROWS; r++)
            for (c = 0; c < COLS; c++)
                peg_e[r][c] = peg_k[r][c] = 0.0f;
        for (i = 0; i <= NDRUM; i++)
            bin[i] = bin_v[i] = 0.0f;
        for (i = 0; i < NVOICE; i++)
            seen_serial[i] = vis->v[i].serial;
    }
    sub_prev = sub;

    /* a sixteenth has passed: everything that was falling has landed, and sets off again */
    if (landed)
        for (i = 0; i < NBALL; i++) {
            Ball *bl = &ball[i];
            int dir;
            if (!bl->on)
                continue;
            if (bl->row >= ROWS) {               /* on the floor: into its bin */
                int b = iclamp((bl->hx - 1) / 2, 0, NDRUM);
                bin_v[b] += 60.0f + 140.0f * bounce;
                bl->on = 0;
                continue;
            }
            bl->x0 = hx_x(bl->hx);
            bl->y0 = row_y(bl->row) - 4.0f;
            bl->u0 = 0.0f;
            if (pin(algo, bl->row, bl->hx)) {    /* off a pin: to one side or the other */
                c = iclamp(bl->hx >> 1, 0, COLS - 1);
                peg_e[bl->row][c] = 1.0f;
                peg_k[bl->row][c] = 1.0f;
                dir = rng_next(vis->rng) & 1u ? 1 : -1;
                if (bl->hx + dir < 1 || bl->hx + dir > 25)
                    dir = -dir;
                bl->hx = (int16_t)(bl->hx + dir);
                bl->fresh = 0;
                bl->spin += (f32)dir * 0.17f;
            } else
                bl->fresh = 1;                   /* nothing there: straight on down */
            bl->row++;
        }

    /* a note: something drops in at the top of its drum's chute */
    for (i = 0; i < NVOICE; i++)
        if (vis->v[i].serial != seen_serial[i]) {
            Ball *bl = &ball[0];
            int j, type = vis->v[i].note % 12;
            seen_serial[i] = vis->v[i].serial;
            for (j = 0; j < NBALL; j++) {        /* a free one, or else the one furthest down */
                if (!ball[j].on) { bl = &ball[j]; break; }
                if (ball[j].row > bl->row) bl = &ball[j];
            }
            bl->on = 1;
            bl->type = (uint8_t)type;
            bl->fresh = 1;
            bl->hx = (int16_t)(2 + type * 2 + (type & 1));          /* (over a pin of the first row, or beside one) */
            if (((bl->hx ^ 0) & 1) == 0)
                bl->hx++;
            bl->row = 0;
            bl->x0 = hx_x(bl->hx);
            bl->y0 = -12.0f;
            bl->u0 = minf(sub, 0.9f);
            bl->spin = (f32)type * 0.13f;
        }

    /* the board */
    g_line(6.0f, 16.0f, 6.0f, 232.0f, 1.2f, 0.3f);
    g_line(234.0f, 16.0f, 234.0f, 232.0f, 1.2f, 0.3f);
    for (i = 0; i <= NDRUM; i++) {               /* the bins, and what has piled up in them */
        f32 x = 12.0f + (f32)i * DX;
        spring_z(&bin[i], &bin_v[i], 0.0f, 9.0f, 0.5f, dt);
        if (bin[i] < 0.0f) bin[i] = 0.0f;
        if (bin[i] > 20.0f) bin[i] = 20.0f;
        g_line(x - DX * 0.5f, FLOOR + 6.0f, x - DX * 0.5f, 234.0f, 1.0f, 0.3f);
        if (bin[i] > 0.5f)
            g_rect(snapf(x - DX * 0.5f + 3.0f, q), 233.0f - snapf(bin[i], q), DX - 6.0f, snapf(bin[i], q), 0.9f);
    }
    g_line(12.0f + 12.5f * DX, FLOOR + 6.0f, 12.0f + 12.5f * DX, 234.0f, 1.0f, 0.3f);
    g_line(3.0f, 234.0f, 237.0f, 234.0f, 1.2f, 0.4f);

    for (r = 0; r < ROWS; r++) {
        G_POLL();
        for (c = 0; c < COLS; c++) {
            int hx = c * 2 + ((r & 1) ? 2 : 1);
            f32 x, y, e, s;
            if (!pin(algo, r, hx)) {
                peg_e[r][c] = 0.0f;
                continue;
            }
            e = peg_e[r][c] = maxf(0.0f, peg_e[r][c] - dt * glow_k * (0.3f + peg_e[r][c]));
            peg_k[r][c] = maxf(0.0f, peg_k[r][c] - dt * 7.0f);
            x = snapf(hx_x(hx), q);
            y = snapf(row_y(r) + peg_k[r][c] * peg_k[r][c] * (1.5f + 5.0f * bounce), q);
            s = (1.5f + 0.5f * scale) * (1.0f + 0.9f * e) + 0.25f * pulse * (f32)((r + c) & 1);
            glyph(algo, x, y, algo == 0 ? maxf(s, q * 0.5f) : s, (f32)(r + c) * 0.07f, 0.34f + 0.66f * sat(e * 2.0f), 0);
            if (e > 0.05f && algo != 0)          /* a pin that was hit throws a ring */
                g_ring(x, y, s + (1.0f - e) * (5.0f + 9.0f * decay), 1.0f, e * 0.6f);
        }
    }

    /* what is falling */
    for (i = 0; i < NBALL; i++) {
        Ball *bl = &ball[i];
        f32 x, y, s;
        int g;
        if (!bl->on)
            continue;
        s = (f32)SIZE[bl->type] * 0.085f * scale;
        if (s < 1.5f)
            s = 1.5f;
        for (g = tails; g >= 1; g--) {           /* DECAY: where it was a moment ago, and before that */
            f32 ug = sub - (f32)g * 0.11f;
            if (ug <= bl->u0)
                continue;
            ball_at(bl, ug, hop, &x, &y);
            glyph(algo, snapf(x, q), snapf(y, q), maxf(s * (1.0f - 0.16f * (f32)g), 1.0f), bl->spin, 0.36f - 0.07f * (f32)g, 0);
        }
        ball_at(bl, sub, hop, &x, &y);
        glyph(algo, snapf(x, q), snapf(y, q), algo == 0 ? maxf(s, q * 0.5f) : s, bl->spin + sub * 0.25f * (bl->fresh ? 0.0f : 1.0f), 1.0f, 1);
    }
}

const InstDef inst_plink = {
    "PLINK",
    {255, 226, 92},
    {"TUNE", "DECAY", "BOUNCE", "BITS"},
    {"8BIT", "ANALOG", "HOUSE", "FM", "ZAP", "GLITCH", "CLICK", "RAVE"},
    {{50, 50, 30, 15}, 0, {0, 50, 100, 75}, {0, 55, 0, 0}, {0, 3, 10, 50}, {0, 3, 1, 60}, {0, 0, 0}},
    0.55f,
    plink_block, plink_note, plink_render, 0, plink_draw,
};
