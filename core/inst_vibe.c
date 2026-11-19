/* h1 - VIBE: a drum machine of recordings. Twelve hits, one on every note of the octave as on BONK (the
 * octave you play it in tunes it), each a real thing struck once (vibe_data.c, docs/SAMPLES.md). The
 * picture is a gamelan left out in the rain at night: a note is a fat drop landing on one of its twelve
 * pieces, which rings, throws a crown of water and swings. The algorithm is the kit, and it changes the
 * weather.
 *
 *   C kick  C# rim  D snare  D# clap  E low tom  F mid tom  F# hat  G high tom
 *   G# shaker  A bell  A# open hat  B crash
 *
 *   K1 TUNE    every hit up or down                    the bronze shrinks or grows
 *   K2 DECAY   how long they ring                      how far the rings on the water go, how long a piece glows
 *   K3 WIND    each hit a little off its pitch, and    the rain slants, the gongs sway on their cords
 *              wavering; further apart, left to right
 *   K4 STORM   every hit lands with a splash, harder   more rain, bigger crowns, and at the top lightning
 */
#include "internal.h"
#include "dsp.h"
#include "vibe_data.h"

/* ---- sound ------------------------------------------------------------------------------ */
enum { KICK, RIM, SNARE, CLAP, TOML, TOMM, HAT, TOMH, SHAKE, COW, OHAT, CRASH, NDRUM };

typedef struct Slot {
    uint8_t zone;
    int8_t semis5;                               /* fifths of a semitone */
    uint8_t level, dec;                          /* percent */
} Slot;

#define NKIT 5
static const Slot KITS[NKIT][NDRUM] = {
    {   /* WORLD: a darbuka and a frame drum under bells, bars and a gong, the bells a gamelan's five steps apart */
        {VZ_DUM, 0, 100, 100}, {VZ_TEK, 0, 90, 100}, {VZ_FRAME, 50, 90, 70}, {VZ_ZILL, 0, 80, 100},
        {VZ_LOG, 0, 100, 100}, {VZ_BALA, 0, 90, 100}, {VZ_SHAKER, 0, 70, 45}, {VZ_BALA, 24, 90, 100},
        {VZ_BELL, 0, 75, 100}, {VZ_AGOGO, -12, 80, 100}, {VZ_BELL, -36, 80, 130}, {VZ_GONG, 0, 100, 100},
    },
    {   /* HANDS: congas, bongo, cajon, and what is shaken and clapped */
        {VZ_TUMBA, -15, 100, 100}, {VZ_CLAVE, 0, 85, 100}, {VZ_QUINTO, 0, 95, 100}, {VZ_CLAP, 0, 90, 100},
        {VZ_TUMBA, 10, 95, 100}, {VZ_BONGO, -25, 95, 100}, {VZ_CABASA, 0, 75, 100}, {VZ_BONGO, 0, 95, 100},
        {VZ_SHAKER, 0, 75, 100}, {VZ_COWBELL, 0, 80, 100}, {VZ_CAJON, 0, 90, 100}, {VZ_TAMB, -15, 85, 160},
    },
    {   /* HALL: the back row of an orchestra */
        {VZ_BASSDRUM, 0, 100, 100}, {VZ_STICK, 0, 90, 100}, {VZ_SNARE, 0, 95, 100}, {VZ_SLAP, 0, 80, 100},
        {VZ_TIMP, -25, 95, 100}, {VZ_TIMP, 0, 95, 100}, {VZ_TRIANGLE, 0, 70, 12}, {VZ_TIMP, 25, 95, 100},
        {VZ_WOODBLOCK, 0, 85, 100}, {VZ_CHIME, 0, 80, 100}, {VZ_TRIANGLE, 0, 75, 100}, {VZ_CYMBAL, 0, 100, 100},
    },
    {   /* SHOP: what was lying about the workshop */
        {VZ_CAJON, -60, 100, 140}, {VZ_WOODBLOCK, -25, 90, 100}, {VZ_BRAKE, 0, 90, 55}, {VZ_SLAP, -20, 85, 100},
        {VZ_BRAKE, -60, 90, 100}, {VZ_ANVIL, -60, 85, 100}, {VZ_HAT, 0, 75, 60}, {VZ_ANVIL, -25, 85, 100},
        {VZ_CABASA, -35, 80, 100}, {VZ_COWBELL, -20, 80, 50}, {VZ_ANVIL, 15, 75, 130}, {VZ_BRAKE, 35, 85, 220},
    },
    {   /* CHIME: every hit a note */
        {VZ_MBIRA, -120, 100, 100}, {VZ_GLOCK, 60, 70, 60}, {VZ_MBIRA, 0, 90, 100}, {VZ_ZILL, 25, 75, 100},
        {VZ_CHIME, -60, 90, 100}, {VZ_CHIME, -35, 90, 100}, {VZ_TRIANGLE, 25, 65, 15}, {VZ_CHIME, -15, 90, 100},
        {VZ_GLOCK, 0, 75, 100}, {VZ_GLOCK, 35, 75, 100}, {VZ_BELL, 0, 75, 100}, {VZ_GONG, 60, 95, 100},
    },
};

static struct {
    int algo;
    f32 tune, dec, wind, storm, wow;
} K;
static uint32_t dice = 0x51D1CEu;

static void vibe_block(const Blk *b)
{
    K.algo = b->algo & 7;
    K.tune = (b->k[0] - 0.5f) * 24.0f;
    K.dec = exp2fx((b->k[1] - 0.5f) * 5.0f);
    K.wind = b->k[2];
    K.storm = b->k[3];
    K.wow = tsin(b->time * 0.7f) + 0.5f * tsin(b->time * 2.3f);
}

static void vibe_note(Voice *v, const Blk *b)
{
    int type = v->note % 12, kit = K.algo < NKIT ? K.algo : (K.algo == 5 ? 0 : 1);
    const Slot *sl = &KITS[kit][type];
    uint32_t zone = sl->zone;
    f32 semis = (f32)sl->semis5 * 0.2f;
    (void)b;
    if (!v->u[0]) v->u[0] = 0x71BE0DEu + v->idx * 977u;
    if (K.algo == 7) {                           /* DICE: any hit of them all, and where it lands is its own business */
        zone = (rng_next(&dice) >> 8) % VZ_N;
        semis = (f32)((int)((rng_next(&dice) >> 8) % 15u) - 7);
    }
    v->q[0] = 0;                                 /* the next stored sample */
    v->q[1] = 0;                                 /* the decoder: its value (a signed number in here) */
    v->q[2] = vibe_zones[zone].idx0;             /* and its step */
    v->q[3] = zone;
    v->u[1] = 0;                                 /* how far between two stored samples, in 1/65536 */
    v->u[2] = 0;                                 /* samples since the hit */
    v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0.0f;        /* the last four decoded */
    v->s[4] = 1.0f;                              /* its dying away */
    v->s[5] = semis + (v->r1 - 0.5f) * 5.0f * K.wind * K.wind;
    v->s[6] = (f32)sl->level * 0.01f;
    v->s[7] = (f32)sl->dec * 0.01f;
    v->s[8] = v->s[9] = 0.0f;                    /* the splash's filter */
    v->s[10] = 1.0f;                             /* and its loudness */
    v->s[11] = 0.0f;                             /* TAPE's dullness */
    v->pan = clampf(((f32)type - 5.5f) * (0.06f + 0.14f * K.wind), -0.85f, 0.85f);
}

static void vibe_render(Voice *v, const Blk *b, f32 *out, int n)
{
    const VibeZone *z = &vibe_zones[v->q[3] < VZ_N ? v->q[3] : 0];
    const uint8_t *d = vibe_data + z->off;
    int type = v->note % 12, i;
    f32 oct = ((f32)v->note - 60.0f - (f32)type) * (1.0f / 12.0f);
    f32 semis = v->s[5] + K.tune + oct * (K.algo == 4 ? 12.0f : 5.0f) + b->bend + K.wow * K.wind * 0.35f
              + (K.algo == 5 ? -12.0f + 0.25f * K.wow : (K.algo == 6 ? 14.0f : 0.0f));
    f32 ratio = exp2fx(clampf(semis * (1.0f / 12.0f), -4.0f, 3.0f));
    uint32_t inc = (uint32_t)(ratio * (f32)z->rate * b->isr * 65536.0f), frac = v->u[1] & 0xFFFFu, idx = v->q[0];
    uint32_t len = z->len, loop = z->loop;
    int32_t pred = (int32_t)v->q[1];
    uint32_t step = v->q[2] > 88u ? 88u : v->q[2];
    f32 dec = K.dec * v->s[7] * (K.algo == 6 ? 0.45f : (K.algo == 5 ? 1.6f : 1.0f));
    f32 kd = 1.0f - onepole_k(maxf(z->tau * dec, 0.004f), b->sr), env = v->s[4];
    f32 a = v->s[0], bb = v->s[1], c = v->s[2], e = v->s[3], gain = z->gain * v->s[6];
    f32 sp = v->s[10], ksp = 1.0f - onepole_k(0.012f + 0.05f * K.storm, b->sr), splash = K.storm * K.storm * 0.5f;
    f32 drive = 1.0f + 3.0f * K.storm, dull = v->s[11], kdull = K.algo == 5 ? 0.22f : 1.0f;
    Svf fs;
    if (inc > (6u << 16))
        inc = 6u << 16;
    if (pred > 32767 || pred < -32768)
        pred = 0;
    svf_set(&fs, 5200.0f + 2500.0f * v->r2, 0.9f, b->isr);
    for (i = 0; i < n; i++) {
        f32 t, x;
        frac += inc;
        while (frac >= 65536u) {                 /* take up the next stored sample (six at most) */
            frac -= 65536u;
            a = bb;
            bb = c;
            c = e;
            if (idx >= len && loop < len) {      /* a ringing hit goes round, the decoder as it was there */
                idx = loop;
                pred = z->lpred;
                step = z->lidx > 88u ? 88u : z->lidx;
            }
            if (idx < len) {
                uint32_t nib = (d[idx >> 1] >> ((idx & 1u) << 2)) & 15u, sz = vibe_steps[step];
                int32_t q = (int32_t)(sz >> 3);
                if (nib & 4u) q += (int32_t)sz;
                if (nib & 2u) q += (int32_t)(sz >> 1);
                if (nib & 1u) q += (int32_t)(sz >> 2);
                pred = iclamp(nib & 8u ? pred - q : pred + q, -32768, 32767);
                step = (uint32_t)iclamp((int32_t)step + ((nib & 4u) ? (int32_t)((nib & 3u) * 2u + 2u) : -1), 0, 88);
                idx++;
                e = (f32)pred * (1.0f / 32768.0f);
            } else
                e = 0.0f;
        }
        t = (f32)frac * (1.0f / 65536.0f);
        x = bb + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * bb + 4.0f * c - e + t * (3.0f * (bb - c) + e - a)));
        x *= env * gain;
        env *= kd;
        if (splash > 0.002f && sp > 0.001f) {    /* STORM: the water it lands in */
            f32 lo, bp, nz = noise_s(&v->u[0]);
            SVF_TICK(fs, &v->s[8], nz, lo, bp);
            x += (nz - lo - bp * fs.k) * sp * splash;
            sp *= ksp;
        }
        dull += kdull * (x - dull);              /* TAPE: the top is gone */
        out[i] = softclip(dull * drive) * (1.0f / (0.6f + 0.4f * drive));
    }
    v->u[1] = frac; v->q[0] = idx; v->q[1] = (uint32_t)pred; v->q[2] = step;
    v->s[0] = a; v->s[1] = bb; v->s[2] = c; v->s[3] = e; v->s[4] = env; v->s[10] = sp; v->s[11] = dull;
}

/* ---- picture ---------------------------------------------------------------------------- */
enum { GONG, POT, BAR, DRUM };
typedef struct Piece { uint8_t kind, x, y, r; } Piece;

/* by drum: the two hanging gongs, five pots on their rack, four bars on their trough, the drum */
static const Piece PIECES[NDRUM] = {
    {DRUM, 196, 190, 20},      /* kick */
    {BAR, 24, 184, 9},         /* rim */
    {BAR, 46, 186, 10},        /* snare */
    {BAR, 70, 188, 11},        /* clap */
    {POT, 138, 142, 13},       /* low tom */
    {POT, 164, 138, 11},       /* mid tom */
    {BAR, 96, 190, 12},        /* hat */
    {POT, 187, 135, 10},       /* high tom */
    {POT, 207, 132, 8},        /* shaker */
    {POT, 224, 130, 7},        /* bell */
    {GONG, 104, 88, 19},       /* open hat */
    {GONG, 50, 96, 32},        /* crash */
};

typedef struct Sky {
    uint8_t n, len;                              /* streaks, and how long each is */
    int16_t speed;                               /* pixels a second; negative: it rains upward */
    int8_t slant;                                /* in 1/100 of its fall */
    uint8_t flake, moon, bolt;                   /* snow instead; 1 a full moon, 2 a thin one; lightning */
} Sky;
static const Sky SKIES[NALGO] = {
    {46, 13, 300, 12, 0, 1, 0},                  /* WORLD: steady rain under a full moon */
    {26, 6, 210, -8, 0, 2, 0},                   /* HANDS: a drizzle */
    {84, 22, 430, 4, 0, 0, 1},                   /* HALL: a downpour */
    {56, 9, 340, 55, 0, 0, 0},                   /* SHOP: sleet, sideways */
    {50, 1, 46, 6, 1, 2, 0},                     /* CHIME: snow */
    {30, 30, 120, 10, 0, 1, 0},                  /* TAPE: slowed down */
    {96, 3, 250, 16, 0, 2, 0},                   /* TINY: a mist */
    {44, 12, -260, -14, 0, 1, 1},                /* DICE: it rains upward */
};

#define NDROP 36
#define NRING 10
static struct { f32 x, y, vx, vy, life; } drop[NDROP];
static struct { f32 x, y, t, w; } ring[NRING];
static f32 glow[NDRUM], swing[NDRUM], swing_v[NDRUM], bolt_t;
static uint32_t seen_serial[NVOICE], bolt_seed;
static int next_drop, next_ring;

static f32 hashf(uint32_t i)
{
    i = i * 747796405u + 2891336453u;
    i = ((i >> ((i >> 28) + 4u)) ^ i) * 277803737u;
    return (f32)((i ^ (i >> 22)) >> 8) * (1.0f / 16777216.0f);
}

/* one piece, r across, lit by e. It leaves where its top is in *sx, *sy: where the rain lands. */
static void piece(int i, f32 x, f32 y, f32 r, f32 e, f32 sw, f32 *sx, f32 *sy)
{
    f32 a = 0.62f + 0.38f * sat(e * 2.5f), w = 1.4f + 1.6f * e, xy[8];
    switch (PIECES[i].kind) {
    case GONG: {                                 /* hung by two cords from the beam, and swinging */
        f32 top = 40.0f, gx = x + sw * (y - top) * 0.5f, gy = y - absf(sw) * 3.0f;
        g_line(x - r * 0.5f, top, gx - r * 0.55f, gy - r * 0.82f, 1.0f, 0.4f);
        g_line(x + r * 0.5f, top, gx + r * 0.55f, gy - r * 0.82f, 1.0f, 0.4f);
        if (e > 0.02f) g_glow(gx, gy, r * (1.2f + 0.6f * e), e * 0.6f);
        g_ring(gx, gy, r, w + 0.6f, a);
        g_ring(gx, gy, r * 0.62f, 1.0f, a * 0.55f);
        g_disc(gx, gy, r * (0.2f + 0.08f * e), a);
        *sx = gx; *sy = gy - r;
        break;
    }
    case POT:                                    /* a kettle on the rack: a dome with a boss */
        g_arc(x, y, r, 0.5f, 1.0f, w, a);
        g_line(x - r - 2.0f, y, x + r + 2.0f, y, w, a);
        g_disc(x, y - r - 1.0f, r * 0.26f + 1.0f + 1.5f * e, a);
        if (e > 0.02f) g_glow(x, y - r * 0.4f, r * (1.0f + e), e * 0.7f);
        *sx = x; *sy = y - r - 3.0f;
        break;
    case BAR:                                    /* a bar on the trough */
        xy[0] = x - 8.0f; xy[1] = y - r * 0.5f - e * 2.0f;
        xy[2] = x + 8.0f; xy[3] = y - r * 0.5f + 2.0f - e * 2.0f;
        xy[4] = x + 8.0f; xy[5] = y + r + 2.0f;
        xy[6] = x - 8.0f; xy[7] = y + r;
        if (e > 0.02f) g_fill(xy, 4, e * 0.8f);
        g_poly(xy, 4, 1, w * 0.8f, a);
        *sx = x; *sy = y - r * 0.5f;
        break;
    default:                                     /* the drum, on its side */
        g_ellipse(x - r, y, r * 0.3f, r * 0.62f, w, a);
        g_ellipse(x + r, y, r * 0.24f, r * 0.5f, w, a * 0.8f);
        g_line(x - r, y - r * 0.62f, x + r, y - r * 0.5f, w, a);
        g_line(x - r, y + r * 0.62f, x + r, y + r * 0.5f, w, a);
        if (e > 0.02f) g_ellipse(x - r, y, r * 0.3f * e, r * 0.62f * e, 0.0f, e * 0.8f);
        g_line(x - r * 0.5f, y + r * 0.6f, x - r * 0.8f, 204.0f, 1.0f, 0.4f);
        g_line(x + r * 0.5f, y + r * 0.55f, x + r * 0.8f, 204.0f, 1.0f, 0.4f);
        *sx = x; *sy = y - r * 0.56f;
        break;
    }
}

static void vibe_draw(const Vis *vis)
{
    f32 dt = vis->dt, t = vis->t, tune = vis->k[0], decay = vis->k[1], wind = vis->k[2], storm = vis->k[3];
    int algo = vis->algo & 7, i, n, flip = algo & 1;
    const Sky *sky = &SKIES[algo];
    f32 scale = 1.25f - 0.5f * tune, slant = (f32)sky->slant * 0.01f + (wind - 0.2f) * 0.9f;
    f32 gust = sin_turns(t * 0.11f) * 0.6f + sin_turns(t * 0.29f) * 0.4f, flash = 0.0f;
    f32 sx[NDRUM], sy[NDRUM];

    if (vis->fresh) {
        for (i = 0; i < NDROP; i++) drop[i].life = 0.0f;
        for (i = 0; i < NRING; i++) ring[i].t = 1.0f;
        for (i = 0; i < NDRUM; i++) glow[i] = swing[i] = swing_v[i] = 0.0f;
        for (i = 0; i < NVOICE; i++) seen_serial[i] = vis->v[i].serial;
        bolt_t = 1.0f;
    }

    /* lightning, when the storm is up (and always now and then where the sky has it) */
    bolt_t += dt * 2.6f;
    if (bolt_t >= 1.0f && storm + (sky->bolt ? 0.35f : 0.0f) > 0.62f && rng_f(vis->rng) < dt * (0.25f + 1.2f * storm)) {
        bolt_t = 0.0f;
        bolt_seed = rng_next(vis->rng);
    }
    if (bolt_t < 1.0f) {
        f32 bx = 30.0f + hashf(bolt_seed) * 180.0f, by = 0.0f;
        flash = (1.0f - bolt_t) * (1.0f - bolt_t);
        for (i = 0; i < 7 && flash > 0.15f; i++) {
            f32 nx = bx + (hashf(bolt_seed + (uint32_t)i * 7u + 1u) - 0.5f) * 44.0f, ny = by + 9.0f + hashf(bolt_seed + (uint32_t)i * 7u + 2u) * 16.0f;
            g_line(bx, by, nx, ny, 1.0f + 2.0f * flash, flash);
            bx = nx; by = ny;
        }
    }

    /* the moon, behind the rain */
    if (sky->moon) {
        f32 mx = flip ? 52.0f : 196.0f, my = 40.0f;
        g_glow(mx, my, 34.0f, 0.22f * (1.0f - 0.7f * storm));
        g_disc(mx, my, 13.0f, 0.75f - 0.45f * storm);
        if (sky->moon == 2) {
            g_mode(G_PAINT);
            g_disc(mx + 6.0f, my - 3.0f, 12.0f, 0.0f);
            g_mode(G_MAX);
        }
    }

    /* the stand the gongs hang from, the pots' rack, the bars' trough, the ground */
    g_origin(0.0f, 0.0f);
    {
        f32 m = flip ? -1.0f : 1.0f, o = flip ? 240.0f : 0.0f;
        g_line(o + m * 6.0f, 40.0f, o + m * 132.0f, 40.0f, 2.2f, 0.6f + 0.4f * flash);
        g_arc(o + m * 6.0f, 34.0f, 6.0f, flip ? 0.0f : 0.25f, flip ? 0.25f : 0.5f, 2.0f, 0.6f);
        g_arc(o + m * 132.0f, 34.0f, 6.0f, flip ? 0.25f : 0.0f, flip ? 0.5f : 0.25f, 2.0f, 0.6f);
        g_line(o + m * 12.0f, 40.0f, o + m * 12.0f, 204.0f, 1.6f, 0.5f);
        g_line(o + m * 126.0f, 40.0f, o + m * 126.0f, 204.0f, 1.6f, 0.5f);
        g_line(o + m * 122.0f, 150.0f, o + m * 236.0f, 136.0f, 1.4f, 0.45f);
        g_line(o + m * 130.0f, 150.0f, o + m * 130.0f, 204.0f, 1.2f, 0.4f);
        g_line(o + m * 232.0f, 137.0f, o + m * 232.0f, 204.0f, 1.2f, 0.4f);
        g_line(o + m * 14.0f, 199.0f, o + m * 112.0f, 206.0f, 1.4f, 0.45f);
    }
    g_line(0.0f, 205.0f, 240.0f, 205.0f, 1.0f, 0.3f + 0.5f * flash);

    /* a note: the rain lands on its piece */
    for (i = 0; i < NDRUM; i++) {
        const Piece *p = &PIECES[i];
        f32 x = flip ? 240.0f - (f32)p->x : (f32)p->x, r = (f32)p->r * (p->kind == BAR ? 1.0f : scale);
        f32 sway = p->kind == GONG ? gust * (0.02f + 0.12f * wind) : 0.0f;
        spring_z(&swing[i], &swing_v[i], sway, 5.0f, 0.12f + 0.2f * (1.0f - decay), dt);
        glow[i] = maxf(0.0f, glow[i] - dt * (0.3f + glow[i]) / (0.15f + 1.1f * decay * decay));
        piece(i, x, (f32)p->y, r, glow[i], clampf(swing[i], -0.5f, 0.5f), &sx[i], &sy[i]);
    }
    for (i = 0; i < NVOICE; i++)
        if (vis->v[i].serial != seen_serial[i]) {
            int ty = vis->v[i].note % 12, k, crown = 4 + (int)(storm * 5.0f);
            seen_serial[i] = vis->v[i].serial;
            glow[ty] = 1.0f;
            swing_v[ty] += (hashf(vis->v[i].serial) - 0.5f) * 5.0f + 1.6f;
            ring[next_ring].x = sx[ty];
            ring[next_ring].y = sy[ty];
            ring[next_ring].t = 0.0f;
            ring[next_ring].w = (f32)PIECES[ty].r;
            next_ring = (next_ring + 1) % NRING;
            for (k = 0; k < crown; k++) {        /* the crown of water it throws */
                f32 an = 0.5f + ((f32)k + 0.5f) / (f32)crown * 0.5f, sp = 55.0f + 70.0f * storm + 30.0f * rng_f(vis->rng);
                drop[next_drop].x = sx[ty];
                drop[next_drop].y = sy[ty];
                drop[next_drop].vx = cos_turns(an) * sp * 0.7f + slant * 30.0f;
                drop[next_drop].vy = sin_turns(an) * sp;
                drop[next_drop].life = 1.0f;
                next_drop = (next_drop + 1) % NDROP;
            }
        }

    /* the rings going out from where it landed, the drop that made them, the crown */
    for (i = 0; i < NRING; i++) {
        f32 u, reach;
        if (ring[i].t >= 1.0f)
            continue;
        u = ring[i].t;
        reach = ring[i].w * (0.8f + 1.6f * decay) + 8.0f;
        if (u < 0.2f)
            g_beam(ring[i].x - slant * 46.0f, ring[i].y - 46.0f, ring[i].x, ring[i].y, 1.0f, 3.2f, 0.0f, 1.0f - u * 5.0f, 0.0f);
        g_ellipse(ring[i].x, ring[i].y, 3.0f + reach * ease_out(u), 1.0f + reach * 0.3f * ease_out(u), 1.6f * (1.0f - u) + 0.4f, 1.0f - u);
        if (u > 0.25f)
            g_ellipse(ring[i].x, ring[i].y, 3.0f + reach * ease_out(u - 0.25f), 1.0f + reach * 0.3f * ease_out(u - 0.25f), 1.0f, 0.6f * (1.0f - u));
        ring[i].t = u + dt / (0.35f + 0.9f * decay);
    }
    for (i = 0; i < NDROP; i++) {
        if (drop[i].life <= 0.0f)
            continue;
        drop[i].vy += 420.0f * dt;
        drop[i].x += drop[i].vx * dt;
        drop[i].y += drop[i].vy * dt;
        drop[i].life -= dt * 1.8f;
        if (drop[i].y > 206.0f)
            drop[i].life = 0.0f;
        else
            g_disc(drop[i].x, drop[i].y, 0.8f + 1.4f * drop[i].life, sat(drop[i].life * 1.6f));
    }

    /* the weather */
    n = (int)((f32)sky->n * (0.45f + 1.3f * storm));
    if (n > 150)
        n = 150;
    for (i = 0; i < n; i++) {
        f32 h = hashf((uint32_t)i * 3u + 1u), sp = (f32)sky->speed * (0.75f + 0.5f * hashf((uint32_t)i * 3u + 2u));
        f32 ph = fractf(h * 7.0f + t * sp * (1.0f / 250.0f)), y = ph * 250.0f - 20.0f, len = (f32)sky->len * (0.6f + 0.8f * h);
        f32 x = fractf(hashf((uint32_t)i * 3u + 3u) + ph * slant + (f32)ifloor(h * 7.0f + t * sp * (1.0f / 250.0f)) * 0.37f) * 260.0f - 10.0f;
        f32 a = 0.22f + 0.4f * h + 0.3f * flash;
        if ((i & 7) == 0)
            G_POLL();
        if (sky->flake) {
            x += sin_turns(t * 0.3f + h * 9.0f) * 6.0f;
            if (y < 204.0f)
                g_disc(x, y, 0.8f + 1.1f * h, a + 0.2f);
        } else if (y < 204.0f || sky->speed < 0)
            g_line(x - slant * len, y - len, x, y, 1.0f, a);
        else if (y < 216.0f)                     /* where it meets the ground: a small ring */
            g_ellipse(x, 208.0f + h * 22.0f, (y - 204.0f) * (0.6f + h), (y - 204.0f) * 0.2f, 1.0f, 0.5f * (1.0f - (y - 204.0f) / 12.0f));
    }
}

const InstDef inst_vibe = {
    "VIBE",
    {150, 222, 255},
    {"TUNE", "DECAY", "WIND", "STORM"},
    {"WORLD", "HANDS", "HALL", "SHOP", "CHIME", "TAPE", "TINY", "DICE"},
    {{50, 50, 20, 15}, 0, {0, 50, 100, 80}, {0, 55, 0, 0}, {0, 3, 18, 50}, {0, 3, 1, 60}, {0, 0, 0}},
    1.3f,
    vibe_block, vibe_note, vibe_render, 0, vibe_draw,
};
