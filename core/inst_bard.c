/* h1 - BARD: the medieval one. Eight old instruments, all of them recordings of real ones (smp.h): three
 * plucked, on the strings of str.h; two bowed, two reeds and a little pipe organ, held for as long as the
 * key is. Over a drone that is recorded too. And a page from a manuscript that writes itself: every note
 * is penned as a square neume on a four-line staff, under a great illuminated initial, with a vine up
 * each margin that flowers on the notes you hold and something small and odd going along the bottom, as
 * there always was.
 *
 *   K1 DRONE   the bourdon under everything             the ground line thickens and hums
 *   K2 COURSE  a second string, pipe or reed beside it   everything written gets a twin
 *   K3 BITE    harder quill, harder reed: brighter       a broader nib, thorns on the vines
 *   K4 AGE     out of tune, breathy, dull                the page wears: flakes, foxing, a shaky hand
 */
#include "internal.h"
#include "dsp.h"
#include "smp.h"
#include "str.h"

/* ---- sound ------------------------------------------------------------------------------
 * What stands in for what: the lute is a gut-strung lyre (COURSE strings it in pairs, as a lute is), the
 * gurdy a cello under a turning wheel, the rebec an erhu (both are small fiddles of two or three gut
 * strings), the shawm a shehnai, its sister. The harp, psaltery, bagpipe and organ are themselves. */
enum { PLUCK, HELD };
typedef struct BAlgo {
    uint8_t kind, bank;
    f32 level;
    f32 ring;                                    /* plucked: decay, against the recording's */
    uint8_t dbank[3];                            /* the drone: three recordings, */
    f32 doff[3];                                 /* how far above the key's root two octaves down, */
    f32 dlev[3];                                 /* and how loud */
} BAlgo;

#define D_STRINGS {SMP_BASSBOW, SMP_CELLO, SMP_CELLO}, {0.0f, 7.02f, 12.07f}, {1.0f, 0.6f, 0.4f}
static const BAlgo BA[NALGO] = {
    /* LUTE     */ {PLUCK, SMP_LUTE, 1.0f, 0.6f, D_STRINGS},
    /* HARP     */ {PLUCK, SMP_FOLKHARP, 1.0f, 1.0f, D_STRINGS},
    /* PSALTERY */ {PLUCK, SMP_PSALTERY, 0.9f, 1.0f, D_STRINGS},
    /* GURDY    */ {HELD, SMP_CELLO, 1.0f, 0.0f, D_STRINGS},
    /* REBEC    */ {HELD, SMP_FIDDLE, 1.0f, 0.0f, D_STRINGS},
    /* SHAWM    */ {HELD, SMP_SHAWM, 0.9f, 0.0f, {SMP_SHAWM, SMP_SHAWM, SMP_SHAWM}, {12.0f, 19.02f, 24.05f}, {1.0f, 0.35f, 0.3f}},
    /* BAGPIPE  */ {HELD, SMP_PIPES, 0.9f, 0.0f, {SMP_PIPES, SMP_PIPES, SMP_PIPES}, {12.0f, 24.03f, 23.97f}, {1.0f, 0.6f, 0.4f}},
    /* ORGAN    */ {HELD, SMP_ORGAN, 1.0f, 0.0f, {SMP_ORGAN, SMP_ORGAN, SMP_ORGAN}, {0.0f, 12.02f, 19.0f}, {1.0f, 0.7f, 0.3f}},
};

#define B_PLUCK 3.4f             /* a string's level against the other instruments */
#define B_HELD 2.0f
#define B_DRONE 1.1f
#define B_FADE 48                /* samples in which a string that is plucked again lets go of what it held */

static struct {
    int algo;
    f32 drone, course, bite, age, gs, ts, tilt, edge, kl, air;
} B;
static Svf drone_f, nose_f;
static SmpR drone_r[3];
static f32 drone_s[2], drone_lv, wheel_ph;
static volatile int sounding;

static void bard_block(const Blk *b)
{
    const BAlgo *a = &BA[b->algo];
    B.algo = b->algo;
    B.drone = b->k[0];
    B.course = b->k[1];
    B.bite = b->k[2];
    B.age = b->k[3];
    /* plucked: at BITE's middle and no AGE the string is the recording's */
    B.gs = (1.0f - 0.45f * B.age) * (0.75f + 0.5f * B.bite);
    B.ts = a->ring * (1.0f - 0.5f * B.age);
    B.tilt = B.bite < 0.5f ? lerpf(0.15f, 1.0f, B.bite * 2.0f) : 1.0f + (B.bite - 0.5f) * 3.0f;
    /* held: BITE lifts the top or takes it off, AGE dulls it and lets the air through */
    B.edge = (B.bite - 0.5f) * (B.bite > 0.5f ? 3.0f : 1.6f);
    B.kl = onepole_k(1.0f / (H1_TAU * (1200.0f + 9000.0f * (1.0f - B.age) * (1.0f - B.age))), b->sr);
    B.air = 0.004f + 0.05f * B.age * B.age;
    wheel_ph = fractf(wheel_ph + 3.1f * b->step);
    svf_set(&drone_f, 500.0f + 2500.0f * B.bite, 1.0f, b->isr);
    svf_set(&nose_f, 1250.0f, 2.2f, b->isr);
}

/* where a second string, pipe or reed sits against the first */
static f32 beside(f32 pitch)
{
    if (B.algo == 7 || (B.algo == 0 && pitch < 57.0f))           /* the organ's 4-foot rank; a lute's low courses */
        return 12.0f + 0.04f;
    return 0.07f + 0.1f * B.age;
}

static void bard_note(Voice *v, const Blk *b)
{
    const BAlgo *a = &BA[B.algo];
    f32 pitch = v->target + b->bend;
    if (!v->u[2]) v->u[2] = 0xBA4D0001u + v->idx * 7919u;
    v->q[1] = (uint32_t)smp_pick(a->bank, pitch);
    v->s[8] = 1.0f;                              /* the attack: the dog barks, the piper flicks a grace note */
    if (a->kind == PLUCK) {
        const SmpZone *z = &smp_zones[v->q[1]];
        Str s;
        smp_fit(z, pitch, &v->ph[4], &v->ph[5]);
        str_set(&s, mtof(pitch), 0.5f, 1.0f, b->sr);
        smp_start(&v->r[0]);                     /* the pluck starts */
        v->q[0] = smp_inc(z, pitch, b->isr);
        v->q[2] = (uint32_t)s.len + 4u;          /* and for one trip round, what the string held is let go */
        v->s[13] = (f32)v->q[2];
        v->s[12] = 0.0f;
        return;
    }
    v->q[3] = (uint32_t)smp_pick(a->bank, pitch + beside(pitch));
    if (B.algo == 6 && v->env >= 0.001f)         /* BAGPIPE: it cannot stop between notes */
        return;
    smp_start(&v->r[0]);
    smp_start(&v->r[1]);
    v->r[1].idx = 37;                            /* (the second player is never quite with the first) */
    v->ph[3] = 0.0f;
    if (v->env < 0.001f)
        v->s[0] = v->s[1] = v->s[4] = v->s[5] = 0.0f;
}

static void pluck_render(Voice *v, const Blk *b, f32 *out, int n, f32 pitch)
{
    const BAlgo *a = &BA[B.algo];
    const SmpZone *z = smp_zone(a->bank, v->q[1], pitch);
    f32 *line = ks_line[v->idx][0], *line2 = ks_line[v->idx][1];
    f32 e[CTRL], tl = v->s[12], c = B.course * 0.9f, hiss = B.age * B.age * 0.004f, lev = a->level * B_PLUCK;
    f32 g = clampf(v->ph[4] * B.gs, 0.05f, 0.985f), t60 = clampf(v->ph[5], 0.05f, 20.0f) * B.ts;
    Str s1, s2;
    uint32_t w = v->u[0] & STR_MASK, w2 = v->u[1] & STR_MASK, kill = v->q[2];
    int i, two = c > 0.01f;

    str_set(&s1, mtof(pitch), g, t60, b->sr);
    str_set(&s2, mtof(pitch + beside(pitch)), g, t60 * 0.85f, b->sr);
    if (!smp_done(z, &v->r[0])) {                /* the recorded pluck at the note's speed, and BITE: what with */
        smp_read(z, &v->r[0], v->q[0], z->gain, e, n, 0);
        for (i = 0; i < n; i++) {
            tl += 0.16f * (e[i] - tl);
            e[i] = tl + B.tilt * (e[i] - tl);
        }
    } else
        for (i = 0; i < n; i++)
            e[i] = 0.0f;
    for (i = 0; i < n; i++) {
        f32 fb = str_back(line, w, &s1, &v->s[0]), o, o2 = 0.0f;
        int old = kill != 0;
        if (kill) {                              /* (the first trip round still carries the last note) */
            fb *= maxf(0.0f, 1.0f - (v->s[13] - (f32)kill) * (1.0f / (f32)B_FADE));
            kill--;
        }
        o = e[i] + fb;
        line[w] = o;
        w = (w + 1u) & STR_MASK;
        if (two) {                               /* COURSE: the string beside it, plucked with it */
            o2 = e[i] * 0.85f + (old ? 0.0f : str_back(line2, w2, &s2, &v->s[2]));
            line2[w2] = o2;
            w2 = (w2 + 1u) & STR_MASK;
        }
        out[i] = (o + c * o2 + hiss * noise_s(&v->u[2])) * lev;
    }
    v->u[0] = w;
    v->u[1] = w2;
    v->q[2] = kill;
    v->s[12] = tl;
}

static void held_render(Voice *v, const Blk *b, f32 *out, int n, f32 pitch)
{
    const BAlgo *a = &BA[B.algo];
    const SmpZone *z = smp_zone(a->bank, v->q[1], pitch), *z2;
    f32 dt = (f32)n * b->isr, atk = v->s[8], lp = v->s[4], hp = v->s[5], fade = v->ph[3], lev = a->level * B_HELD, p2;
    f32 kh = onepole_k(1.0f / (H1_TAU * 900.0f), b->sr), amp = 1.0f, buzz = 0.0f, nose = 0.0f, lo, bp;
    int i;

    if (B.algo == 4) {                           /* REBEC: the bow's vibrato comes in late */
        v->ph[2] = fractf(v->ph[2] + 5.6f * dt);
        pitch += 0.16f * tsin(v->ph[2]) * sat(v->age * 2.5f - 0.5f);
    }
    if (B.algo == 6)                             /* BAGPIPE: between two notes it flicks a grace note */
        pitch += v->age < 0.03f ? 5.0f : (v->age < 0.058f ? -2.0f : 0.0f);
    if (B.algo == 3) {                           /* GURDY: the wheel turns, and the dog barks at the start of a note */
        amp = 1.0f - 0.14f * (0.5f + 0.5f * tsin(wheel_ph));
        buzz = atk * (0.5f + 2.5f * B.bite);
        nose = 0.5f;
    }
    if (B.algo == 5 || B.algo == 6)              /* the reeds: blown harder, they rasp */
        buzz = maxf(0.0f, B.bite - 0.5f) * 1.2f;
    smp_read(z, &v->r[0], smp_inc(z, pitch, b->isr), z->gain, out, n, 0);
    if (B.course > 0.01f) {
        p2 = pitch + beside(pitch);
        z2 = smp_zone(a->bank, v->q[3], p2);
        smp_read(z2, &v->r[1], smp_inc(z2, p2, b->isr), z2->gain * B.course * (B.algo == 7 ? 0.6f : 0.8f), out, n, 1);
    }
    for (i = 0; i < n; i++) {
        f32 x = out[i] * amp;
        if (nose > 0.0f) {                       /* the gurdy's small box */
            SVF_TICK(nose_f, &v->s[0], x, lo, bp);
            (void)lo;
            x = x * 0.85f + bp * nose_f.k * nose;
        }
        if (buzz > 0.01f)
            x = softclip(x * (1.0f + 6.0f * buzz)) / (1.0f + 1.5f * buzz);
        hp += kh * (x - hp);
        x += B.edge * (x - hp);                  /* BITE */
        lp += B.kl * (x - lp);                   /* AGE */
        fade += (1.0f - fade) * 0.008f;
        out[i] = (lp + B.air * noise_s(&v->u[2])) * fade * lev;
    }
    v->s[4] = lp;
    v->s[5] = hp;
    v->ph[3] = fade;
    v->s[8] = atk * (1.0f - onepole_k(0.09f, b->sr / (f32)n));
}

static void bard_render(Voice *v, const Blk *b, f32 *out, int n)
{
    f32 pitch = v->pitch + b->bend;
    if (n > CTRL) n = CTRL;
    sounding = 1;
    /* AGE: every string and pipe wanders by itself */
    v->s[10] += (rng_s(&v->u[2]) - v->s[10]) * 0.02f;
    v->s[9] += (v->s[10] - v->s[9]) * 0.05f;
    pitch += B.age * (v->s[9] * 0.9f + (v->r1 - 0.5f) * 0.35f);
    if (BA[B.algo].kind == PLUCK)
        pluck_render(v, b, out, n, pitch);
    else
        held_render(v, b, out, n, pitch);
}

/* The drone: recordings too, two octaves under the key's root and above it. It comes in under the first
 * note and goes when the playing stops. */
static void bard_post(const Blk *b, f32 *l, f32 *r, int n)
{
    const BAlgo *a = &BA[B.algo];
    f32 root = 36.0f + (f32)st.keys[0] + b->bend, lv = B.drone * B.drone * B_DRONE, d[CTRL];
    f32 want = sounding ? 1.0f : 0.0f, k = sounding ? 0.004f : 0.0006f, lo, bp;
    int i, j;
    sounding = 0;
    if (n > CTRL) n = CTRL;
    if (lv * drone_lv <= 0.0005f) {
        drone_lv += (want - drone_lv) * minf(1.0f, k * (f32)n);
        return;
    }
    if (a->kind == PLUCK || B.algo == 3 || B.algo == 4)          /* strings: the wheel that bows them */
        lv *= 1.0f - 0.12f * (0.5f + 0.5f * tsin(wheel_ph));
    for (j = 0; j < 3; j++) {
        f32 p = root + a->doff[j];
        const SmpZone *z = &smp_zones[smp_pick(a->dbank[j], p)];
        smp_read(z, &drone_r[j], smp_inc(z, p, b->isr), z->gain * a->dlev[j], d, n, j > 0);
    }
    for (i = 0; i < n; i++) {
        f32 x;
        SVF_TICK(drone_f, drone_s, d[i], lo, bp);
        (void)bp;
        drone_lv += (want - drone_lv) * k;
        x = lo * lv * drone_lv;
        l[i] += x;
        r[i] += x;
    }
}


/* ---- picture ---------------------------------------------------------------------------- */
#define NNM 54
typedef struct Neume {
    uint8_t staff, live, down, tied;
    uint8_t voice;
    uint32_t serial;
    f32 x, y, born, gone;
} Neume;
static Neume nm[NNM];
static int nm_w, cur_staff;
static f32 cur_x, last_t, last_y, crawl, hopv, hop, ears, pagel, drone_vis;
static f32 bud[12];
static uint32_t seen_s[NVOICE];
static const char *const INITIAL[NALGO] = {"L", "H", "P", "G", "R", "S", "B", "O"};
static const f32 STAFF[3][3] = {{92.0f, 228.0f, 26.0f}, {28.0f, 228.0f, 98.0f}, {28.0f, 228.0f, 146.0f}};   /* x0, x1, top line */
#define SGAP 7.0f

static f32 hsh(uint32_t n) { n = (n ^ 61u) ^ (n >> 16); n *= 9u; n ^= n >> 4; n *= 0x27d4eb2du; n ^= n >> 15; return (f32)(n & 0xFFFFu) * (1.0f / 65536.0f); }

static f32 pitch_dy(f32 pitch)
{
    f32 d = clampf(pitch - 50.0f, -200.0f, 200.0f);
    while (d > 30.0f) d -= 12.0f;
    while (d < -3.0f) d += 12.0f;
    return 3.0f * SGAP + 5.0f - d * 1.0f;        /* below the top line */
}

/* a leaf: a sharp little almond, from the stem at (x, y) pointing along (dx, dy) */
static void leaf(f32 x, f32 y, f32 dx, f32 dy, f32 len, f32 a)
{
    f32 xy[8], px = -dy, py = dx, w = len * 0.32f;
    xy[0] = x; xy[1] = y;
    xy[2] = x + dx * len * 0.45f + px * w; xy[3] = y + dy * len * 0.45f + py * w;
    xy[4] = x + dx * len; xy[5] = y + dy * len;
    xy[6] = x + dx * len * 0.45f - px * w; xy[7] = y + dy * len * 0.45f - py * w;
    g_fill(xy, 4, a * 0.85f);
    g_line(x, y, xy[4], xy[5], 1.0f, a);
}

/* one margin's vine, from (x, y0) down to (x, y1). side = which way the first leaf leans. */
static void vine(f32 x, f32 y0, f32 y1, int first_bud, const Vis *vis, f32 course, f32 bite, f32 shake)
{
    f32 xy[2 * 34], t = vis->t, len = y1 - y0;
    int i, n = 33, twin;
    for (twin = 0; twin < 2; twin++) {
        f32 sgn = twin ? -1.0f : 1.0f, a = twin ? course : 1.0f;
        if (a < 0.03f)
            continue;
        for (i = 0; i <= n; i++) {
            f32 u = (f32)i / (f32)n, y = y0 + len * u;
            xy[2 * i] = x + sgn * 3.6f * sin_turns(y * (1.0f / 30.0f) + 0.02f * sin_turns(t * 0.11f + u)) + shake * (hsh((uint32_t)(i * 7 + twin)) - 0.5f) * 1.5f;
            xy[2 * i + 1] = y;
        }
        g_poly(xy, n + 1, 0, twin ? 1.1f : 1.7f, (twin ? 0.65f : 0.9f) * a);
    }
    for (i = 0; i < 9; i++) {                    /* leaves, thorns and six buds, turn about */
        f32 y = y0 + len * ((f32)i + 0.5f) / 9.0f, sx = x + 3.6f * sin_turns(y * (1.0f / 30.0f)), side = (i & 1) ? 1.0f : -1.0f;
        f32 sway = 0.03f * sin_turns(t * 0.23f + (f32)i * 0.31f), dx = side * cos_turns(0.09f + sway), dy = -sin_turns(0.09f + sway);
        if (i % 3 == 1) {                        /* a bud: it opens while its pitch sounds */
            int pc = first_bud + i / 3;
            f32 o = bud[pc % 12], bx = sx + dx * 6.0f, by = y + dy * 6.0f;
            int k;
            g_line(sx, y, bx, by, 1.0f, 0.8f);
            if (o > 0.04f)
                for (k = 0; k < 5; k++) {
                    f32 an = (f32)k / 5.0f + t * 0.05f;
                    g_disc(bx + cos_turns(an) * 3.6f * o, by + sin_turns(an) * 3.6f * o, 1.0f + 1.7f * o, 0.6f + 0.4f * o);
                }
            g_disc(bx, by, 1.5f + 0.8f * o, 1.0f);
        } else
            leaf(sx, y, dx, dy, 7.5f + 1.5f * sin_turns(t * 0.17f + (f32)i), 0.8f);
        if (bite > 0.35f) {                      /* BITE: thorns */
            f32 tl = (bite - 0.35f) * 9.0f;
            g_line(sx, y + 5.0f, sx - side * tl, y + 5.0f - tl * 0.5f, 1.0f, 0.9f);
        }
    }
}

/* what goes along the bottom of the page: a snail, a hare or a wyrm */
static void creature(int kind, f32 x, f32 gy, const Vis *vis, f32 lev)
{
    f32 t = vis->t;
    int i;
    if (kind == 0) {                             /* the snail: a shell that is one long spiral */
        f32 xy[2 * 26], cx = x, cy = gy - 12.0f - hop * 0.3f;
        for (i = 0; i < 26; i++) {
            f32 u = (f32)i / 25.0f, r = 11.0f * (1.0f - 0.82f * u), an = 0.5f - u * 2.3f;
            xy[2 * i] = cx + cos_turns(an) * r;
            xy[2 * i + 1] = cy + sin_turns(an) * r;
        }
        g_poly(xy, 26, 0, 1.8f, 1.0f);
        g_line(x - 13.0f, gy - 2.5f, x + 15.0f, gy - 2.5f, 4.0f, 1.0f);            /* the foot */
        g_line(x + 14.0f, gy - 3.0f, x + 19.0f, gy - 9.0f, 3.2f, 1.0f);            /* the neck */
        for (i = 0; i < 2; i++) {                /* two horns that reach for the music */
            f32 wv = 0.04f * sin_turns(t * 0.7f + (f32)i * 0.3f) + 0.06f * ears, an = -0.2f + (f32)i * 0.09f - wv;
            f32 hx = x + 19.0f + cos_turns(an) * (7.0f + 4.0f * lev), hy = gy - 9.0f + sin_turns(an) * (7.0f + 4.0f * lev);
            g_line(x + 19.0f, gy - 9.0f, hx, hy, 1.2f, 1.0f);
            g_disc(hx, hy, 1.6f, 1.0f);
        }
    } else if (kind == 1) {                      /* the hare: all ears */
        f32 by = gy - 9.0f - hop, e = 0.06f * ears;
        g_ellipse(x, by, 11.0f, 7.0f, 0.0f, 1.0f);
        g_disc(x + 11.0f, by - 7.0f, 5.0f, 1.0f);
        g_line(x + 10.0f, by - 10.0f, x + 6.0f + e * 40.0f, by - 25.0f, 2.6f, 1.0f);
        g_line(x + 12.5f, by - 10.0f, x + 12.0f + e * 60.0f, by - 26.0f, 2.6f, 1.0f);
        g_disc(x - 11.0f, by - 3.0f, 2.6f, 1.0f);
        g_line(x - 6.0f, by + 5.0f, x - 10.0f - hop * 0.5f, gy - 1.0f, 2.4f, 1.0f);
        g_line(x + 6.0f, by + 5.0f, x + 9.0f + hop * 0.5f, gy - 1.0f, 2.0f, 1.0f);
        g_mode(G_PAINT);
        g_disc(x + 13.0f, by - 8.0f, 1.1f, 0.0f);
        g_mode(G_MAX);
    } else {                                     /* the wyrm: a line with opinions */
        f32 xy[2 * 20], hx, hy;
        for (i = 0; i < 20; i++) {
            f32 u = (f32)i / 19.0f;
            xy[2 * i] = x - 22.0f + 40.0f * u;
            xy[2 * i + 1] = gy - 7.0f - 5.0f * sin_turns(u * 1.6f - t * 0.9f) * (0.4f + 0.6f * u) - hop * 0.2f;
        }
        g_poly(xy, 20, 0, 3.4f, 1.0f);
        hx = xy[38]; hy = xy[39];
        leaf(hx - 2.0f, hy, 1.0f, -0.12f, 10.0f, 1.0f);
        g_line(hx + 8.0f, hy - 1.0f, hx + 12.0f + 6.0f * ears, hy - 3.0f, 1.0f, 1.0f);       /* the tongue */
        g_line(hx + 8.0f, hy - 1.0f, hx + 12.0f + 6.0f * ears, hy + 1.5f, 1.0f, 1.0f);
        for (i = 4; i < 16; i += 4)              /* and a few spines */
            g_line(xy[2 * i], xy[2 * i + 1] - 1.5f, xy[2 * i] - 2.0f, xy[2 * i + 1] - 6.5f, 1.2f, 0.9f);
        g_mode(G_PAINT);
        g_disc(hx + 3.0f, hy - 0.5f, 0.9f, 0.0f);
        g_mode(G_MAX);
    }
}

static void bard_draw(const Vis *vis)
{
    f32 dt = vis->dt, t = vis->t, drone = vis->k[0], course = vis->k[1], bite = vis->k[2], age = vis->k[3], mt = smooth01(vis->algo_t);
    f32 lev, nib = 4.0f + 3.4f * bite, shake = age * age, want[12] = {0}, gy = 221.0f;
    int i, j, algo = vis->algo;

    if (vis->fresh) {
        for (i = 0; i < NNM; i++)
            nm[i].live = 0;
        {                                        /* the page was used before: an older tune shows through */
            f32 gyy = 12.0f;
            int k = 0;
            for (j = 0; j < 3; j++)
                for (i = 0; STAFF[j][0] + 12.0f + (f32)i * 13.0f < STAFF[j][1] - 8.0f && k < 36; i++) {
                    Neume *q = &nm[k];
                    gyy = clampf(gyy + (hsh((uint32_t)k * 7u + 3u) - 0.5f) * 9.0f, 0.0f, 24.0f);
                    q->staff = (uint8_t)j; q->live = 3; q->x = STAFF[j][0] + 12.0f + (f32)i * 13.0f; q->y = gyy;
                    q->born = -9.0f; q->down = hsh((uint32_t)k + 40u) > 0.6f; q->tied = 0; q->voice = 0; q->serial = 0xFFFFFFFFu;
                    k++;
                }
            nm_w = k % NNM;
        }
        for (i = 0; i < NVOICE; i++)
            seen_s[i] = vis->v[i].serial;
        for (i = 0; i < 12; i++)
            bud[i] = 0.0f;
        cur_staff = 0;
        cur_x = STAFF[0][0] + 8.0f;
        last_t = -9.0f;
        hop = hopv = ears = 0.0f;
        pagel = 0.0f;
    }
    lev = sat(vis->level * 3.0f);
    pagel += (lev - pagel) * minf(1.0f, dt * 8.0f);
    drone_vis += ((lev > 0.02f ? drone : drone * 0.45f) - drone_vis) * minf(1.0f, dt * 3.0f);

    /* the scribe: a new note is a new neume */
    for (i = 0; i < NVOICE; i++) {
        const VVoice *v = &vis->v[i];
        if (v->on) {
            int pc = ((int)(v->pitch + 0.5f) % 12 + 12) % 12;
            if (v->env > want[pc]) want[pc] = v->env;
        }
        if (v->serial != seen_s[i]) {
            Neume *q;
            f32 y = pitch_dy(v->pitch);
            seen_s[i] = v->serial;
            if (t - last_t > 0.045f) {           /* (notes struck together share a place: a chord is a stack) */
                cur_x += nib + 5.0f + (t - last_t > 0.6f ? 5.0f : 0.0f);
                if (cur_x > STAFF[cur_staff][1] - 8.0f) {
                    cur_staff = (cur_staff + 1) % 3;
                    cur_x = STAFF[cur_staff][0] + 9.0f;
                    for (j = 0; j < NNM; j++)    /* the old line is scraped off as the new one goes over it */
                        if (nm[j].live == 1 && nm[j].staff == cur_staff) {
                            nm[j].live = 2;
                            nm[j].gone = t;
                        }
                }
            }
            for (j = 0; j < NNM; j++)            /* (the new ink covers the old) */
                if (nm[j].live == 3 && nm[j].staff == cur_staff && absf(nm[j].x - cur_x) < 9.0f) {
                    nm[j].live = 2;
                    nm[j].gone = t;
                }
            q = &nm[nm_w];
            nm_w = (nm_w + 1) % NNM;
            q->staff = (uint8_t)cur_staff;
            q->live = 1;
            q->x = cur_x;
            q->y = y;
            q->born = t;
            q->down = y > last_y + 0.5f;
            q->tied = t - last_t < 0.4f && t - last_t > 0.045f;
            q->voice = (uint8_t)i;
            q->serial = v->serial;
            last_t = t;
            last_y = y;
            hopv = -70.0f;
            ears = 1.0f;
        }
    }
    for (i = 0; i < 12; i++)
        bud[i] += (sat(want[i] * 1.6f) - bud[i]) * minf(1.0f, dt * (want[i] > bud[i] ? 16.0f : 2.5f));
    hopv += 420.0f * dt;
    hop -= hopv * dt;
    if (hop < 0.0f) { hop = 0.0f; hopv = 0.0f; }
    ears = maxf(0.0f, ears - dt * 2.2f);
    crawl = fractf(crawl + dt * (0.012f + 0.0005f * (f32)st.glo[0]) * (algo % 3 == 1 ? 2.2f : 1.0f));

    g_dim(1.0f - 0.32f * age);

    /* the frame */
    g_line(3.0f, 4.0f, 237.0f, 4.0f, 1.0f, 0.55f);
    g_line(3.0f, 8.0f, 237.0f, 8.0f, 2.0f, 0.9f);
    for (i = 0; i < 24; i++)
        g_disc(8.0f + (f32)i * 9.75f, 12.5f, 0.9f, 0.5f + 0.5f * (i % 12 < 12 ? bud[i % 12] : 0.0f));

    /* the vines */
    vine(11.0f, 88.0f, 212.0f, 0, vis, course, bite, shake);
    vine(234.0f, 58.0f, 212.0f, 6, vis, course, bite, shake);

    /* the staves, and what has been written on them */
    for (j = 0; j < 3; j++) {
        for (i = 0; i < 4; i++) {
            f32 y = STAFF[j][2] + (f32)i * SGAP, wob = shake * 1.2f * (hsh((uint32_t)(j * 4 + i)) - 0.5f);
            g_line(STAFF[j][0], y + wob, STAFF[j][1], y - wob, 1.0f, 0.42f);
            if (course > 0.04f)
                g_line(STAFF[j][0], y + wob + 1.6f, STAFF[j][1], y - wob + 1.6f, 1.0f, 0.3f * course);
        }
        /* the clef: a C, as they wrote it */
        g_rect(STAFF[j][0] - 1.0f, STAFF[j][2] + SGAP * 0.5f - 0.5f, 3.0f, SGAP * 2.0f + 1.0f, 0.9f);
        g_rect(STAFF[j][0] + 2.0f, STAFF[j][2] + SGAP * 0.5f - 0.5f, 3.0f, 3.0f, 0.9f);
        g_rect(STAFF[j][0] + 2.0f, STAFF[j][2] + SGAP * 2.5f - 2.5f, 3.0f, 3.0f, 0.9f);
    }
    for (i = 0; i < NNM; i++) {
        Neume *q = &nm[i];
        const Neume *p = &nm[(i + NNM - 1) % NNM];
        f32 x, y, a = 1.0f, z = nib, wet, sing = 0.0f, tw;
        int k;
        if (!q->live)
            continue;
        if (q->live == 2) {
            a = 1.0f - (t - q->gone) * 2.5f;
            if (a <= 0.0f) { q->live = 0; continue; }
            if (q->serial == 0xFFFFFFFFu) a *= 0.3f;
        } else if (q->live == 3)
            a = 0.3f;
        wet = sat((t - q->born) * 7.0f);
        if (vis->v[q->voice].on && vis->v[q->voice].serial == q->serial)
            sing = vis->v[q->voice].env;
        x = q->x + shake * 2.0f * (hsh(q->serial * 3u + 1u) - 0.5f);
        y = STAFF[q->staff][2] + q->y + shake * 3.0f * (hsh(q->serial * 5u + 2u) - 0.5f);
        z *= 1.0f + 0.9f * (1.0f - wet) + 0.25f * sing;
        for (k = (course > 0.04f ? 1 : 0); k >= 0; k--) {
            tw = k ? 2.0f + 3.0f * course : 0.0f;
            if (q->down || algo == 2 || algo == 5) {             /* a falling note leans: a lozenge */
                f32 d[8];
                d[0] = x + tw; d[1] = y + tw * 0.4f - z * 0.75f;
                d[2] = x + tw + z * 0.75f; d[3] = y + tw * 0.4f;
                d[4] = x + tw; d[5] = y + tw * 0.4f + z * 0.75f;
                d[6] = x + tw - z * 0.75f; d[7] = y + tw * 0.4f;
                g_fill(d, 4, a * (k ? 0.5f * course : 1.0f));
            } else {
                g_rect(x + tw - z * 0.5f, y + tw * 0.4f - z * 0.5f, z, z, a * (k ? 0.5f * course : 1.0f));
                if (algo != 7)                   /* its tail */
                    g_line(x + tw + z * 0.5f - 0.5f, y + tw * 0.4f, x + tw + z * 0.5f - 0.5f, y + tw * 0.4f + z + 4.0f, 1.0f, a * (k ? 0.4f * course : 0.9f));
            }
        }
        if (q->tied && p->live && p->staff == q->staff && p->x < q->x)        /* sung in one breath: joined */
            g_line(p->x + nib * 0.5f, STAFF[p->staff][2] + p->y, x - z * 0.5f, y, 1.0f, a * 0.8f);
        if (sing > 0.03f)
            g_ring(x, y, z * 0.5f + 3.5f + 3.0f * sing, 1.0f, a * sat(sing * 2.0f) * 0.8f);
    }
    /* the quill, where the next note will go */
    {
        f32 qx = cur_x + nib + 5.0f, qy = STAFF[cur_staff][2] + last_y - 3.0f + 1.5f * sin_turns(t * 0.4f) - 5.0f * ears, xy[10];
        if (qx > STAFF[cur_staff][1] - 4.0f) qx = STAFF[cur_staff][1] - 4.0f;
        xy[0] = qx; xy[1] = qy;
        xy[2] = qx + 5.0f; xy[3] = qy - 8.0f;
        xy[4] = qx + 12.0f; xy[5] = qy - 15.0f;
        xy[6] = qx + 21.0f; xy[7] = qy - 20.0f;
        xy[8] = qx + 30.0f; xy[9] = qy - 22.0f;
        g_poly(xy, 5, 0, 1.3f, 0.95f);
        for (i = 1; i < 5; i++) {                /* the barbs */
            g_line(xy[2 * i], xy[2 * i + 1], xy[2 * i] + 3.0f, xy[2 * i + 1] - 6.0f, 1.0f, 0.75f);
            g_line(xy[2 * i], xy[2 * i + 1], xy[2 * i] + 6.0f, xy[2 * i + 1] + 2.0f, 1.0f, 0.75f);
        }
    }

    /* the heavens, between the first line and the second: a sun in splendour, a moon, the fixed stars */
    {
        f32 sx = 204.0f, sy = 72.0f, mx = 112.0f, my = 72.0f, turn = t * 0.02f;
        g_disc(sx, sy, 6.0f + 1.5f * pagel, 1.0f);
        for (i = 0; i < 12; i++) {
            f32 an = (f32)i / 12.0f + turn, c = cos_turns(an), sn = sin_turns(an), r0 = 9.5f, r1 = (i & 1 ? 14.0f : 17.0f) + 5.0f * pagel;
            if (i & 1) {                         /* every other ray is a flame */
                f32 xy[8], px = -sn, py = c;
                for (j = 0; j < 4; j++) {
                    f32 u = (f32)j / 3.0f, r = lerpf(r0, r1, u), wv = 1.6f * sin_turns(u * 1.0f + t * 0.5f) * u;
                    xy[2 * j] = sx + c * r + px * wv;
                    xy[2 * j + 1] = sy + sn * r + py * wv;
                }
                g_poly(xy, 4, 0, 1.2f, 0.9f);
            } else
                g_line(sx + c * r0, sy + sn * r0, sx + c * r1, sy + sn * r1, 1.5f, 0.95f);
        }
        g_disc(mx, my, 8.0f, 0.95f);
        g_mode(G_PAINT);
        g_disc(mx + 4.0f, my - 1.5f, 7.0f, 0.0f);
        g_mode(G_MAX);
        for (i = 0; i < 7; i++) {
            f32 qx = 132.0f + (f32)i * 9.0f, qy = 66.0f + 12.0f * hsh((uint32_t)i + 300u), tw = 1.5f + 1.5f * (0.5f + 0.5f * sin_turns(t * 0.3f + (f32)i * 0.37f)) + 2.0f * bud[i];
            g_line(qx - tw, qy, qx + tw, qy, 1.0f, 0.8f);
            g_line(qx, qy - tw, qx, qy + tw, 1.0f, 0.8f);
        }
    }

    /* the great initial */
    {
        f32 bx = 20.0f, by = 20.0f, bs = 60.0f, glow = 0.55f + 0.45f * pagel, lw = 4.5f + 3.0f * bite;
        const char *ch = INITIAL[mt < 0.5f ? vis->algo_prev : algo];
        f32 la = absf(2.0f * mt - 1.0f);
        g_line(bx, by, bx + bs, by, 2.2f, 1.0f); g_line(bx, by + bs, bx + bs, by + bs, 2.2f, 1.0f);
        g_line(bx, by, bx, by + bs, 2.2f, 1.0f); g_line(bx + bs, by, bx + bs, by + bs, 2.2f, 1.0f);
        g_line(bx + 4.0f, by + 4.0f, bx + bs - 4.0f, by + 4.0f, 1.0f, 0.7f); g_line(bx + 4.0f, by + bs - 4.0f, bx + bs - 4.0f, by + bs - 4.0f, 1.0f, 0.7f);
        g_line(bx + 4.0f, by + 4.0f, bx + 4.0f, by + bs - 4.0f, 1.0f, 0.7f); g_line(bx + bs - 4.0f, by + 4.0f, bx + bs - 4.0f, by + bs - 4.0f, 1.0f, 0.7f);
        for (i = -6; i <= 6; i++) {              /* the ground behind the letter: a diaper of crossed lines and dots */
            f32 c = (f32)i * 8.0f, lo = maxf(-26.0f, c - 26.0f), hi = minf(26.0f, c + 26.0f);
            if (hi > lo) {
                g_line(bx + 30.0f + lo, by + 30.0f + lo - c, bx + 30.0f + hi, by + 30.0f + hi - c, 1.0f, 0.3f * glow);
                g_line(bx + 30.0f + lo, by + 30.0f - lo + c, bx + 30.0f + hi, by + 30.0f - hi + c, 1.0f, 0.3f * glow);
            }
        }
        for (i = 0; i < 4; i++) {
            f32 jx = bx + ((i & 1) ? bs : 0.0f), jy = by + ((i & 2) ? bs : 0.0f);
            g_mode(G_PAINT);
            g_disc(jx, jy, 5.0f, 0.0f);
            g_mode(G_MAX);
            g_ring(jx, jy, 4.0f, 1.2f, 1.0f);
            g_disc(jx, jy, 1.2f + 1.6f * pagel, 1.0f);
        }
        g_mode(G_PAINT);
        g_text(bx + bs * 0.5f, by + 11.0f, ch, 38.0f, lw + 7.0f, 0.0f, 1);
        g_mode(G_MAX);
        g_text(bx + bs * 0.5f, by + 11.0f, ch, 38.0f, lw + 2.5f, la, 1);
        g_mode(G_PAINT);                         /* a versal: the letter is drawn as its outline, with a line down its middle */
        g_text(bx + bs * 0.5f, by + 11.0f, ch, 38.0f, lw - 0.5f, 0.0f, 1);
        g_mode(G_MAX);
        g_text(bx + bs * 0.5f, by + 11.0f, ch, 38.0f, 1.0f + 1.5f * pagel, la * (0.5f + 0.5f * pagel), 1);
        /* a tendril that leaves the box and wanders down to the vine */
        g_arc(bx, by + bs + 8.0f, 8.0f, 0.75f, 1.0f + 0.25f, 1.2f, 0.8f);
        leaf(bx - 8.0f, by + bs + 8.0f, -0.5f, 0.86f, 8.0f, 0.8f);
    }

    /* the bottom of the page: the ground is the drone, and somebody is walking on it */
    {
        f32 xy[2 * 31], amp = drone_vis * (1.2f + 3.0f * pagel), cx = 34.0f + crawl * 124.0f;
        for (i = 0; i <= 30; i++) {
            f32 u = (f32)i / 30.0f;
            xy[2 * i] = 22.0f + 196.0f * u;
            xy[2 * i + 1] = gy + 3.0f + amp * sin_turns(u * 6.0f - t * 1.3f) * sin_turns(u * 0.5f);
        }
        g_poly(xy, 31, 0, 1.0f + 5.0f * drone_vis, 0.6f + 0.4f * drone_vis);
        {                                        /* far hills and a small castle, as in the corner of any miniature */
            f32 fl = 2.5f * sin_turns(t * 0.9f) * (0.4f + pagel), w0 = 178.0f, w1 = 224.0f, wy = 203.0f, tri[6];
            g_arc(70.0f, gy + 46.0f, 60.0f, 0.57f, 0.93f, 1.0f, 0.45f);
            g_arc(150.0f, gy + 70.0f, 86.0f, 0.6f, 0.86f, 1.0f, 0.4f);
            g_mode(G_PAINT);                     /* (the hills stop at the ground) */
            g_rect(0.0f, gy + 5.0f, 240.0f, 240.0f - gy - 5.0f, 0.0f);
            g_mode(G_MAX);
            g_line(w0, wy, w1, wy, 1.2f, 0.85f);
            g_line(w0, wy, w0, gy + 2.0f, 1.2f, 0.85f); g_line(w1, wy, w1, gy + 2.0f, 1.2f, 0.85f);
            for (i = 0; i < 6; i++)
                g_rect(w0 + 5.0f + (f32)i * 7.0f, wy - 3.0f, 3.5f, 3.0f, 0.85f);
            for (i = 0; i < 2; i++) {
                f32 tx = i ? w1 - 5.0f : w0 + 5.0f, ty = 190.0f - (f32)i * 5.0f;
                g_rect(tx - 5.0f, ty, 10.0f, wy - ty, 0.0f);
                g_line(tx - 5.0f, ty, tx - 5.0f, wy, 1.2f, 0.9f); g_line(tx + 5.0f, ty, tx + 5.0f, wy, 1.2f, 0.9f);
                tri[0] = tx - 7.0f; tri[1] = ty; tri[2] = tx + 7.0f; tri[3] = ty; tri[4] = tx; tri[5] = ty - 10.0f;
                g_fill(tri, 3, 0.9f);
                g_line(tx, ty - 10.0f, tx, ty - 17.0f, 1.0f, 0.9f);
                g_line(tx, ty - 17.0f, tx + 6.0f, ty - 15.5f + fl, 1.0f, 0.9f);
                g_line(tx, ty - 14.0f, tx + 6.0f, ty - 15.5f + fl, 1.0f, 0.9f);
                g_rect(tx - 1.0f, ty + 4.0f, 2.0f, 4.0f, 0.8f);
            }
            g_arc(201.0f, gy + 2.0f, 6.0f, 0.5f, 1.0f, 1.2f, 0.85f);
            g_line(195.0f, gy + 2.0f, 195.0f, gy - 1.0f, 1.2f, 0.85f); g_line(207.0f, gy + 2.0f, 207.0f, gy - 1.0f, 1.2f, 0.85f);
        }
        g_dim((1.0f - 0.32f * age) * sat(crawl * 12.0f) * sat((1.0f - crawl) * 12.0f));
        creature(algo % 3, cx, gy, vis, pagel);
        g_dim(1.0f - 0.32f * age);
        for (i = 0; i < 5; i++) {                /* tufts */
            f32 tx = 30.0f + (f32)i * 44.0f + 9.0f * hsh((uint32_t)i + 77u);
            g_line(tx, gy + 1.0f, tx - 2.0f, gy - 4.0f, 1.0f, 0.6f);
            g_line(tx, gy + 1.0f, tx + 2.5f, gy - 5.0f, 1.0f, 0.6f);
        }
    }

    /* AGE: flakes gone from the page, foxing left on it */
    if (age > 0.03f) {
        int n = (int)(age * age * 60.0f) + (int)(age * 10.0f);
        g_mode(G_PAINT);
        for (i = 0; i < n; i++) {
            f32 fx = hsh((uint32_t)i * 3u + 11u) * 240.0f, fy = hsh((uint32_t)i * 3u + 12u) * 240.0f, fr = 1.0f + 5.0f * hsh((uint32_t)i * 3u + 13u) * age;
            g_disc(fx, fy, fr, 0.0f);
            if (i % 3 == 0)
                g_line(fx, fy, fx + 14.0f * (hsh((uint32_t)i + 90u) - 0.5f), fy + 12.0f, 1.0f, 0.0f);
        }
        g_mode(G_MAX);
        for (i = 0; i < n / 2; i++)
            g_ring(hsh((uint32_t)i * 5u + 31u) * 240.0f, hsh((uint32_t)i * 5u + 32u) * 240.0f, 1.5f + 3.0f * hsh((uint32_t)i + 55u), 1.0f, 0.22f);
    }
    g_dim(1.0f);
}

const InstDef inst_bard = {
    "BARD",
    {240, 198, 116},
    {"DRONE", "COURSE", "BITE", "AGE"},
    {"LUTE", "HARP", "PSALTERY", "GURDY", "REBEC", "SHAWM", "BAGPIPE", "ORGAN"},
    {{30, 35, 50, 10}, 0, {2, 55, 100, 46}, {0, 55, 0, 0}, {10, 4, 34, 50}, {0, 3, 1, 60}, {0, 0, 0}},
    0.3f,
    bard_block, bard_note, bard_render, bard_post, bard_draw,
};
