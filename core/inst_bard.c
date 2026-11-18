/* h1 - BARD: the medieval one. Eight old instruments (three plucked on real delay-line strings,
 * two bowed, two reeds and a little pipe organ) over a drone, and a page from a manuscript that
 * writes itself: every note is penned as a square neume on a four-line staff, under a great
 * illuminated initial, with a vine up each margin that flowers on the notes you hold and
 * something small and odd going along the bottom, as there always was.
 *
 *   K1 DRONE   the bourdon under everything             the ground line thickens and hums
 *   K2 COURSE  a second string, pipe or reed beside it   everything written gets a twin
 *   K3 BITE    harder quill, harder reed: brighter       a broader nib, thorns on the vines
 *   K4 AGE     out of tune, breathy, dull                the page wears: flakes, foxing, a shaky hand
 */
#include "internal.h"
#include "dsp.h"

#define KS_MASK (KS_LEN - 1)

/* ---- sound ------------------------------------------------------------------------------ */
enum { PLUCK, BOW, REED, PIPE };
typedef struct BAlgo {
    uint8_t kind;
    f32 ring, bright, pick;                      /* plucked: decay, loop filter, where it is plucked */
    f32 f1, f2, q;                               /* the body, or the reed's two formants */
    f32 duty, att;
} BAlgo;

static const BAlgo BA[NALGO] = {
    /* LUTE     */ {PLUCK, 0.55f, 0.8f, 0.24f, 190.0f, 430.0f, 3.0f, 0.0f, 0.0f},
    /* HARP     */ {PLUCK, 1.5f, 0.93f, 0.46f, 240.0f, 560.0f, 2.5f, 0.0f, 0.0f},
    /* PSALTERY */ {PLUCK, 2.2f, 0.99f, 0.09f, 520.0f, 1400.0f, 2.0f, 0.0f, 0.0f},
    /* GURDY    */ {BOW, 0, 0, 0, 620.0f, 1500.0f, 3.5f, 0.0f, 0.05f},
    /* REBEC    */ {BOW, 0, 0, 0, 430.0f, 1180.0f, 4.0f, 0.0f, 0.08f},
    /* SHAWM    */ {REED, 0, 0, 0, 1250.0f, 2700.0f, 4.5f, 0.13f, 0.02f},
    /* BAGPIPE  */ {REED, 0, 0, 0, 860.0f, 2300.0f, 5.0f, 0.22f, 0.012f},
    /* ORGAN    */ {PIPE, 0, 0, 0, 0.0f, 0.0f, 1.0f, 0.0f, 0.035f},
};

static struct {
    int algo;
    f32 drone, course, bite, age, g, t60;
} B;
static Svf body_f[2], drone_f;
static f32 body_s[2][2], drone_s[2], drone_ph[3], drone_lv, wheel_ph, dc_y;
static volatile int sounding;

static void bard_block(const Blk *b)
{
    const BAlgo *a = &BA[b->algo];
    B.algo = b->algo;
    B.drone = b->k[0];
    B.course = b->k[1];
    B.bite = b->k[2];
    B.age = b->k[3];
    B.g = clampf((0.35f + 0.6f * B.bite) * a->bright * (1.0f - 0.35f * B.age), 0.08f, 0.98f);
    B.t60 = 4.5f * a->ring * (1.0f - 0.4f * B.age);
    wheel_ph = fractf(wheel_ph + 3.1f * b->step);
    if (a->kind == PLUCK) {
        svf_set(&body_f[0], a->f1, a->q, b->isr);
        svf_set(&body_f[1], a->f2, a->q + 1.0f, b->isr);
    }
    svf_set(&drone_f, 500.0f + 1500.0f * B.bite, 1.2f, b->isr);
}

static void pluck(f32 *line, uint32_t w, f32 L, f32 p, f32 amp, uint32_t *rng)
{
    int n = (int)L + 1, i;
    f32 iL = 1.0f / L, hard = sat(B.bite * 1.6f - 0.2f), fuzz = 0.08f + 0.5f * B.bite * B.bite;
    for (i = 0; i < n; i++) {
        f32 u = minf((f32)i * iL, 1.0f), tri, pul;
        tri = (u < p ? u / p : (1.0f - u) / (1.0f - p)) * 2.0f - 1.0f;
        pul = u < p ? 1.0f - p : -p;
        line[(w - (uint32_t)n + (uint32_t)i) & KS_MASK] = (lerpf(tri, pul * 1.3f, hard) + rng_s(rng) * fuzz) * amp;
    }
}

static f32 string_len(f32 pitch, const Blk *b)
{
    f32 L = b->sr / mtof(pitch) - (1.0f - B.g) / B.g;
    L = minf(L, 1.0e6f);                         /* (the loop below ends, whatever the pitch was) */
    while (L > (f32)(KS_LEN - 4))
        L *= 0.5f;
    return maxf(L, 2.0f);
}

static void bard_note(Voice *v, const Blk *b)
{
    const BAlgo *a = &BA[B.algo];
    int i;
    if (!v->u[2]) v->u[2] = 0xBA4D0001u + v->idx * 7919u;
    if (a->kind == PLUCK) {
        f32 L = string_len(v->target + b->bend, b);
        v->s[0] = v->s[2] = 0.0f;
        pluck(ks_line[v->idx][0], v->u[0], L, a->pick, 0.85f, &v->u[2]);
        pluck(ks_line[v->idx][1], v->u[1], L * (B.algo == 0 ? 0.5f : 1.0f) * 1.0035f, a->pick * 0.8f, 0.8f, &v->u[2]);
    } else if (v->env < 0.001f) {
        for (i = 0; i < 8; i++)
            v->s[i] = 0.0f;
        v->ph[0] = v->ph[1] = v->ph[3] = 0.0f;
    }
    v->s[8] = 1.0f;                              /* the attack: chiff, buzz, grace note */
}

static void bard_render(Voice *v, const Blk *b, f32 *out, int n)
{
    const BAlgo *a = &BA[B.algo];
    f32 dt = (f32)n * b->isr, pitch = v->pitch + b->bend, atk = v->s[8];
    int i;

    sounding = 1;
    /* AGE: every string and pipe wanders by itself */
    v->s[10] += (rng_s(&v->u[2]) - v->s[10]) * 0.02f;
    v->s[9] += (v->s[10] - v->s[9]) * 0.05f;
    pitch += B.age * (v->s[9] * 0.9f + (v->r1 - 0.5f) * 0.35f);

    if (a->kind == PLUCK) {
        f32 *line = ks_line[v->idx][0], *line2 = ks_line[v->idx][1];
        f32 L = string_len(pitch, b), L2 = maxf(L * (B.algo == 0 ? 0.5f : 1.0f) * 1.0035f, 2.0f);
        f32 rho = exp2fx(-9.966f * L * b->isr / B.t60), rho2 = exp2fx(-9.966f * L2 * b->isr / (B.t60 * 0.85f));
        f32 lp = v->s[0], lp2 = v->s[2], g = B.g, c = B.course * 0.9f, hiss = B.age * B.age * 0.012f;
        uint32_t w = v->u[0] & KS_MASK, w2 = v->u[1] & KS_MASK;
        for (i = 0; i < n; i++) {
            f32 pos = (f32)(w + KS_LEN) - L, fr, y, y2;
            int ip = (int)pos;
            fr = pos - (f32)ip;
            y = line[ip & KS_MASK];
            y += (line[(ip + 1) & KS_MASK] - y) * fr;
            lp += g * (y - lp);
            line[w] = lp * rho;
            w = (w + 1u) & KS_MASK;
            pos = (f32)(w2 + KS_LEN) - L2;
            ip = (int)pos;
            fr = pos - (f32)ip;
            y2 = line2[ip & KS_MASK];
            y2 += (line2[(ip + 1) & KS_MASK] - y2) * fr;
            lp2 += g * (y2 - lp2);
            line2[w2] = lp2 * rho2;
            w2 = (w2 + 1u) & KS_MASK;
            out[i] = y + c * y2 + hiss * noise_s(&v->u[2]);
        }
        v->u[0] = w; v->u[1] = w2;
        v->s[0] = lp; v->s[2] = lp2;
        return;
    }

    {
        Svf f1, f2;
        f32 ph = v->ph[0], ph1 = v->ph[1], d, d1, amp = sat(v->age / a->att), c = B.course, lo, b1, b2;
        f32 cut = (500.0f + 5500.0f * B.bite * B.bite) * exp2fx(-1.4f * B.age), kl = onepole_k(1.0f / (H1_TAU * cut), b->sr), lp = v->s[4], dc = v->s[12];
        f32 air = (0.01f + 0.07f * B.age * B.age) * (a->kind == PIPE ? 1.6f : 1.0f), buzz = 0.0f, drive = 1.0f;

        if (B.algo == 4) {                       /* REBEC: the bow's vibrato comes in late */
            v->ph[2] = fractf(v->ph[2] + 5.6f * dt);
            pitch += 0.16f * tsin(v->ph[2]) * sat(v->age * 2.5f - 0.5f);
        }
        if (B.algo == 6)                         /* BAGPIPE: it cannot stop between notes, so it flicks a grace note */
            pitch += v->age < 0.03f ? 5.0f : (v->age < 0.058f ? -2.0f : 0.0f);
        if (B.algo == 3) {                       /* GURDY: the wheel turns, and the dog barks at the start of a note */
            amp *= 1.0f - 0.14f * (0.5f + 0.5f * tsin(wheel_ph));
            buzz = atk * (0.5f + 2.5f * B.bite);
        }
        if (a->kind == REED)
            drive = 1.0f + 2.0f * B.bite;
        d = minf(mtof(pitch) * b->isr, 0.45f);
        d1 = minf(d * (a->kind == PIPE ? 2.0f : 1.0f) * exp2fx((0.09f + 0.1f * B.age) * (1.0f / 12.0f)), 0.45f);
        svf_set(&f1, a->f1 > 0.0f ? a->f1 : 800.0f, a->q, b->isr);
        svf_set(&f2, a->f2 > 0.0f ? a->f2 : 2000.0f, a->q + 1.0f, b->isr);

        for (i = 0; i < n; i++) {
            f32 x, nz = noise_s(&v->u[2]);
            switch (a->kind) {
            default:
            case BOW:
                x = saw_bl(ph, d) + c * 0.8f * saw_bl(ph1, d1) + nz * (0.04f + air);
                if (buzz > 0.01f)
                    x = clampf(x * (1.0f + 5.0f * buzz), -1.0f, 1.0f);
                break;
            case REED:
                x = pulse_bl(ph, d, a->duty) + c * 0.8f * pulse_bl(ph1, d1, a->duty * 1.3f) + nz * air;
                x = softclip(x * drive);
                break;
            case PIPE:
                x = tsin(ph) + 0.45f * tsin(2.0f * ph) + (0.1f + 0.35f * B.bite) * tsin(3.0f * ph) + 0.18f * B.bite * tsin(4.0f * ph)
                    + c * (0.6f * tsin(ph1) + 0.25f * tsin(ph1 * 1.5f)) + nz * (air + atk * 0.8f);
                break;
            }
            if (a->kind == PIPE) {
                lp += (x - lp) * 0.6f;
                out[i] = lp * 0.42f * amp;
            } else {
                SVF_TICK(f1, &v->s[0], x, lo, b1);
                SVF_TICK(f2, &v->s[2], x, lo, b2);
                (void)lo;
                lp += (x - lp) * kl;
                dc += (lp - dc) * 0.002f;        /* a driven reed leans to one side: take the lean out */
                out[i] = ((lp - dc) * 0.5f + b1 * f1.k * 0.9f + b2 * f2.k * (0.35f + 0.6f * B.bite)) * amp * 0.62f;
            }
            ph += d; if (ph >= 1.0f) ph -= 1.0f;
            ph1 += d1; if (ph1 >= 1.0f) ph1 -= 1.0f;
        }
        v->ph[0] = ph; v->ph[1] = ph1;
        v->s[4] = lp;
        v->s[12] = dc;
        v->s[8] = atk * (1.0f - onepole_k(B.algo == 3 ? 0.09f : 0.03f, b->sr / (f32)n));
    }
}

static void bard_post(const Blk *b, f32 *l, f32 *r, int n)
{
    const BAlgo *a = &BA[B.algo];
    f32 root = 36.0f + (f32)st.keys[0] + b->bend, d0 = mtof(root) * b->isr, d1 = mtof(root + (B.algo == 6 ? 12.0f : 7.02f)) * b->isr;
    f32 d2 = mtof(root + 12.07f) * b->isr, lv = B.drone * B.drone * (B.algo == 6 ? 0.34f : 0.24f), mix = a->kind == PLUCK ? 1.4f : 0.0f;
    f32 want = sounding ? 1.0f : 0.0f, k = sounding ? 0.004f : 0.0006f, duty = a->kind == REED ? 0.2f : 0.5f;
    int i;
    sounding = 0;
    for (i = 0; i < n; i++) {
        f32 m = (l[i] + r[i]) * 0.5f, lo, b0, b1, x = 0.0f;
        if (mix > 0.0f) {
            SVF_TICK(body_f[0], body_s[0], m, lo, b0);
            SVF_TICK(body_f[1], body_s[1], m, lo, b1);
            x = (b0 * body_f[0].k + b1 * body_f[1].k * 0.7f) * mix;
        }
        drone_lv += (want - drone_lv) * k;
        if (lv * drone_lv > 0.0005f) {
            f32 dr = a->kind == PLUCK || a->kind == BOW
                         ? saw_bl(drone_ph[0], d0) + 0.7f * saw_bl(drone_ph[1], d1) + 0.4f * saw_bl(drone_ph[2], d2)
                         : pulse_bl(drone_ph[0], d0, duty) + 0.7f * pulse_bl(drone_ph[1], d1, duty) + 0.4f * pulse_bl(drone_ph[2], d2, duty);
            SVF_TICK(drone_f, drone_s, dr, lo, b0);
            (void)b0;
            x += lo * lv * drone_lv;
            drone_ph[0] += d0; if (drone_ph[0] >= 1.0f) drone_ph[0] -= 1.0f;
            drone_ph[1] += d1; if (drone_ph[1] >= 1.0f) drone_ph[1] -= 1.0f;
            drone_ph[2] += d2; if (drone_ph[2] >= 1.0f) drone_ph[2] -= 1.0f;
        }
        dc_y += 0.002f * (x - dc_y);
        l[i] += x - dc_y;
        r[i] += x - dc_y;
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
static f32 cur_x, last_t, last_y, ears, pagel, drone_vis;
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

/* ---- who lives at the bottom of the page: a snail, a hare or a wyrm ---------------------------
 * None of them slides. Each has a little life of its own: it rests, looks about, makes up its mind,
 * goes a short way in its own gait, turns round at the edge of its ground or when it feels like it,
 * and starts at a note. */
enum { CR_REST, CR_GO, CR_LAND, CR_TURN };
static struct {
    f32 x, dir, t, dur, x0, x1, hgt, gait, tilt, head, amp, rear;
    uint8_t state, left, pose, kind, ready;
} cr;
#define CR_XMIN 46.0f
#define CR_XMAX 160.0f

static void cr_go(int kind, uint32_t *rng)
{
    cr.state = CR_GO;
    cr.t = 0.0f;
    cr.x0 = cr.x;
    if (kind == 1) {                             /* a hop */
        cr.dur = 0.3f + 0.08f * rng_f(rng);
        cr.x1 = clampf(cr.x + cr.dir * (13.0f + 11.0f * rng_f(rng)), CR_XMIN - 4.0f, CR_XMAX + 4.0f);
        cr.hgt = 6.0f + 5.0f * rng_f(rng);
    } else                                       /* a stretch of crawling */
        cr.dur = kind == 0 ? 2.5f + 5.0f * rng_f(rng) : 1.2f + 2.8f * rng_f(rng);
}

static void cr_rest(int kind, uint32_t *rng)
{
    uint32_t r = rng_next(rng) >> 8;
    cr.state = CR_REST;
    cr.t = 0.0f;
    cr.dur = (kind == 1 ? 0.7f : 1.2f) + 2.8f * rng_f(rng);
    cr.pose = (uint8_t)(r % 5u < 2u ? 0 : (r % 5u < 4u ? 1 : 2));     /* just sitting; looking about; busy with something */
}

static void cr_update(int kind, const Vis *vis, int note)
{
    f32 dt = vis->dt, want_tilt = 0.0f, want_head = 0.0f, want_amp = 1.0f, want_rear = 0.0f;
    uint32_t *rng = vis->rng;
    if (!cr.ready || vis->fresh || cr.kind != kind) {
        if (!cr.ready || vis->fresh) {
            cr.x = 70.0f + 60.0f * rng_f(rng);
            cr.dir = 1.0f;
        }
        cr.kind = (uint8_t)kind;
        cr.ready = 1;
        cr.gait = 0.0f;
        cr_rest(kind, rng);
    }
    cr.t += dt;
    switch (cr.state) {
    default:
    case CR_REST:
        if (kind == 1) {                         /* the hare sits up to look, or puts its nose down to the grass */
            if (cr.pose == 1) want_tilt = 0.13f * sat(cr.t * 3.0f) * sat((cr.dur - cr.t) * 3.0f);
            if (cr.pose == 2) want_head = 1.0f;
        } else if (kind == 0) {                  /* the snail draws its horns in, or feels the air */
            if (cr.pose == 2) want_head = 1.0f;
            if (cr.pose == 1) want_rear = 1.0f;
        } else {                                 /* the wyrm lies nearly still, or rears */
            want_amp = 0.35f;
            if (cr.pose != 0) want_rear = 1.0f;
        }
        if (note && kind == 1 && cr.t > 0.12f)   /* a note startles the hare into going */
            cr.dur = minf(cr.dur, cr.t + 0.1f * rng_f(rng));
        if (cr.t >= cr.dur) {
            int out = (cr.x > CR_XMAX && cr.dir > 0.0f) || (cr.x < CR_XMIN && cr.dir < 0.0f);
            if (out || (rng_next(rng) >> 8) % 5u == 0u) {
                cr.state = CR_TURN;
                cr.t = 0.0f;
                cr.dur = kind == 1 ? 0.22f : 0.7f;
            } else {
                cr.left = (uint8_t)(kind == 1 ? (rng_next(rng) >> 8) % 4u : 0u);
                cr_go(kind, rng);
            }
        }
        break;
    case CR_TURN:                                /* it gathers itself, and comes out facing the other way */
        want_head = 1.0f;
        want_amp = 0.15f;
        if (cr.t >= cr.dur * 0.5f && cr.left != 99u) {
            cr.dir = -cr.dir;
            cr.left = 99u;
        }
        if (cr.t >= cr.dur) {
            cr.left = (uint8_t)(kind == 1 ? 1u + (rng_next(rng) >> 8) % 3u : 0u);
            cr_go(kind, rng);
        }
        break;
    case CR_GO:
        if (kind == 1) {
            f32 q = sat(cr.t / cr.dur);
            cr.x = lerpf(cr.x0, cr.x1, q * (1.6f - 0.6f * q));
            want_tilt = (0.5f - q) * 0.16f;      /* nose up as it leaves the ground, down as it comes back */
            if (cr.t >= cr.dur) {
                cr.x = cr.x1;
                cr.state = CR_LAND;
                cr.t = 0.0f;
                cr.dur = 0.07f + 0.12f * rng_f(rng);
            }
        } else {
            f32 sp = kind == 0 ? 0.5f : 1.1f, pull;
            cr.gait = fractf(cr.gait + dt * sp);
            pull = kind == 0 ? maxf(0.0f, sin_turns(cr.gait)) : 0.75f + 0.25f * sin_turns(cr.gait * 2.0f);     /* the snail goes a pull at a time */
            cr.x += cr.dir * dt * (kind == 0 ? 7.5f * pull * pull : 17.0f * pull) * sat(cr.t * 3.0f) * sat((cr.dur - cr.t) * 3.0f);
            if (cr.t >= cr.dur || (cr.x > CR_XMAX + 6.0f && cr.dir > 0.0f) || (cr.x < CR_XMIN - 6.0f && cr.dir < 0.0f))
                cr_rest(kind, rng);
        }
        break;
    case CR_LAND:
        want_tilt = -0.03f;
        if (cr.t >= cr.dur) {
            int out = (cr.x > CR_XMAX && cr.dir > 0.0f) || (cr.x < CR_XMIN && cr.dir < 0.0f);
            if (cr.left && !out) {
                cr.left--;
                cr_go(kind, rng);
            } else
                cr_rest(kind, rng);
        }
        break;
    }
    cr.tilt += (want_tilt - cr.tilt) * minf(1.0f, dt * 14.0f);
    cr.head += (want_head - cr.head) * minf(1.0f, dt * (kind == 0 ? 3.0f : 7.0f));
    cr.amp += (want_amp - cr.amp) * minf(1.0f, dt * 4.0f);
    cr.rear += (want_rear - cr.rear) * minf(1.0f, dt * 3.5f);
}

static void creature(int kind, f32 gy, const Vis *vis, f32 lev)
{
    f32 t = vis->t, x = cr.x, f = cr.dir;
    int i;
    if (kind == 0) {                             /* the snail: a shell that is one long spiral */
        f32 xy[2 * 26], pull = cr.state == CR_GO ? sin_turns(cr.gait) : 0.0f, reach = 3.0f * pull * sat(cr.t * 2.0f);     /* the head goes first, the shell is hauled after */
        f32 in = sat(maxf(cr.head, ears * 1.4f)), cx = x - f * reach * 0.5f, cy = gy - 12.5f - 0.6f * absf(pull), nx = x + f * (14.0f + reach), ny = gy - 3.0f;
        f32 hx = nx + f * 5.0f * (1.0f - in), hy = ny - (6.0f + 2.5f * cr.rear) * (1.0f - in);
        for (i = 0; i < 26; i++) {
            f32 u = (f32)i / 25.0f, r = 11.0f * (1.0f - 0.82f * u), an = 0.5f - u * 2.3f;
            xy[2 * i] = cx + f * cos_turns(an) * r;
            xy[2 * i + 1] = cy + sin_turns(an) * r;
        }
        g_poly(xy, 26, 0, 1.8f, 1.0f);
        g_line(cx + f * 6.5f, cy - 8.5f, cx + f * 9.5f, cy - 2.0f, 1.0f, 0.75f);                 /* the growth lines of the shell */
        g_line(cx - f * 1.0f, cy - 10.8f, cx + f * 1.0f, cy - 7.0f, 1.0f, 0.75f);
        g_line(cx - f * 8.0f, cy - 7.0f, cx - f * 5.5f, cy - 4.0f, 1.0f, 0.75f);
        g_beam(x - f * (15.0f + reach * 0.3f), gy - 1.6f, x - f * 6.0f, gy - 2.5f, 1.4f, 4.0f, 1.0f, 1.0f, 0.0f);     /* the foot: a tail that tapers, */
        g_line(x - f * 6.0f, gy - 2.5f, nx, gy - 2.5f, 4.0f, 1.0f);
        g_line(nx - f * 1.0f, ny, hx, hy, 3.2f, 1.0f);                                         /* a neck, */
        for (i = 0; i < 2; i++) {                /* and two horns that feel for the music, and shrink from it when it comes */
            f32 wv = 0.05f * sin_turns(t * (0.5f + 0.3f * cr.rear) + (f32)i * 0.3f) * (1.0f + 1.5f * cr.rear), an = 0.2f - (f32)i * 0.1f + wv;
            f32 L = (7.0f + 4.0f * lev + 2.0f * cr.rear) * (1.0f - 0.85f * in), tx = hx + f * cos_turns(an) * L, ty = hy - sin_turns(an) * L;
            g_line(hx, hy, tx, ty, 1.2f, 1.0f);
            g_disc(tx, ty, 1.6f, 1.0f);
        }
    } else if (kind == 1) {                      /* the hare */
        f32 q = cr.state == CR_GO ? sat(cr.t / cr.dur) : 0.0f, air = cr.state == CR_GO ? 4.0f * q * (1.0f - q) : 0.0f;
        f32 c = cos_turns(cr.tilt), sn = sin_turns(cr.tilt), bx = x, by = gy - 9.5f - cr.hgt * air - 5.0f * maxf(cr.tilt, 0.0f) * (1.0f - air) * 8.0f;
        f32 nod = cr.head * (1.0f - air), ea, tw = cr.state == CR_REST ? maxf(0.0f, sin_turns(t * 0.37f + cr.x * 0.1f) - 0.93f) * 14.0f : 0.0f;
#define HX(lx, ly) (bx + f * ((lx) * c + (ly) * sn))
#define HY(lx, ly) (by + (ly) * c - (lx) * sn)
        f32 hdx = 11.0f + 2.0f * nod, hdy = -8.0f + 9.0f * nod + 0.6f * sin_turns(t * 3.0f) * nod;
        /* the hind leg: folded under it when it sits, flung out behind in the air, reaching for the ground as it lands */
        if (air > 0.05f) {
            f32 back = sat(1.0f - q * 1.6f), fwd = sat(q * 2.0f - 1.0f);
            g_line(HX(-6.0f, 3.0f), HY(-6.0f, 3.0f), HX(-9.0f - 7.0f * back + 9.0f * fwd, 7.5f + 2.0f * fwd), HY(-9.0f - 7.0f * back + 9.0f * fwd, 7.5f + 2.0f * fwd), 3.0f, 1.0f);
            g_line(HX(5.0f, 2.0f), HY(5.0f, 2.0f), HX(9.0f + 5.0f * (1.0f - back), 6.0f + 3.0f * fwd), HY(9.0f + 5.0f * (1.0f - back), 6.0f + 3.0f * fwd), 2.2f, 1.0f);
        } else {
            g_line(bx - f * 9.0f, gy - 1.6f, bx + f * 0.5f, gy - 1.6f, 3.0f, 1.0f);            /* a long foot flat on the ground */
            g_line(HX(6.0f, 2.0f), HY(6.0f, 2.0f), bx + f * (7.5f + 20.0f * maxf(cr.tilt, 0.0f)), cr.tilt > 0.05f ? HY(9.0f, 5.0f) : gy - 1.2f, 2.2f, 1.0f);
        }
        g_disc(HX(-5.0f, 0.5f), HY(-5.0f, 0.5f), 7.4f, 1.0f);                                  /* haunch */
        g_line(HX(-3.0f, -0.5f), HY(-3.0f, -0.5f), HX(6.0f, -1.5f), HY(6.0f, -1.5f), 11.0f, 1.0f);   /* barrel */
        g_disc(HX(-12.5f, -3.0f), HY(-12.5f, -3.0f), 2.7f, 1.0f);                              /* scut */
        g_line(HX(7.0f, -3.5f), HY(7.0f, -3.5f), HX(hdx - 1.0f, hdy + 1.0f), HY(hdx - 1.0f, hdy + 1.0f), 6.0f, 1.0f);   /* neck */
        g_disc(HX(hdx, hdy), HY(hdx, hdy), 4.7f, 1.0f);
        g_disc(HX(hdx + 3.4f, hdy + 1.6f), HY(hdx + 3.4f, hdy + 1.6f), 2.7f, 1.0f);            /* muzzle */
        /* the ears: laid back in the air, up at a sound, and one of them twitches when nothing is happening */
        ea = 0.33f + 0.1f * air - 0.1f * ears - 0.05f * cr.head * 0.0f;
        for (i = 0; i < 2; i++) {
            f32 an = ea + (f32)i * 0.045f - (i ? tw * 0.05f : 0.0f) + 0.01f * sin_turns(t * 0.21f + (f32)i), L = 15.0f - (f32)i * 1.5f;
            f32 rx = hdx - 1.5f + (f32)i * 1.6f, ry = hdy - 3.2f, mx = rx + cos_turns(an) * L * 0.55f, my = ry - sin_turns(an) * L * 0.55f, tx = rx + cos_turns(an + 0.02f) * L, ty = ry - sin_turns(an + 0.02f) * L;
            g_line(HX(rx, ry), HY(rx, ry), HX(mx, my), HY(mx, my), 3.0f, 1.0f);
            g_beam(HX(mx, my), HY(mx, my), HX(tx, ty), HY(tx, ty), 3.0f, 1.2f, 1.0f, 1.0f, 0.0f);
        }
        g_mode(G_PAINT);
        g_disc(HX(hdx + 1.2f, hdy - 1.0f), HY(hdx + 1.2f, hdy - 1.0f), 1.1f, 0.0f);            /* an eye */
        g_arc(HX(-5.0f, 0.5f), HY(-5.0f, 0.5f), 5.2f, f > 0.0f ? 0.62f : 0.68f, f > 0.0f ? 0.82f : 0.88f, 1.0f, 0.0f);   /* the line of the thigh */
        g_mode(G_MAX);
#undef HX
#undef HY
    } else {                                     /* the wyrm: a line with opinions */
        f32 xy[2 * 20], hx, hy, k = cr.gait, lift = 9.0f * cr.rear, fl = maxf(0.0f, sin_turns(t * 0.9f)) * (0.3f + 0.7f * cr.rear) + ears;
        for (i = 0; i < 20; i++) {
            f32 u = (f32)i / 19.0f;
            xy[2 * i] = x + f * (-22.0f + 40.0f * u - 3.0f * cr.rear * u * u);
            xy[2 * i + 1] = gy - 6.5f - 5.0f * cr.amp * sin_turns(u * 1.6f - k * 2.0f) * (0.4f + 0.6f * u) * (1.0f - 0.5f * cr.rear * u) - lift * u * u * u;
        }
        g_beam(xy[0] - f * 7.0f, gy - 3.0f, xy[0], xy[1], 1.0f, 3.4f, 1.0f, 1.0f, 0.0f);       /* the tail comes to a point */
        g_poly(xy, 20, 0, 3.4f, 1.0f);
        hx = xy[38]; hy = xy[39];
        leaf(hx - f * 2.0f, hy, f, -0.12f - 0.2f * cr.rear, 10.0f, 1.0f);
        g_line(hx + f * 8.0f, hy - 1.0f - 2.0f * cr.rear, hx + f * (10.0f + 7.0f * sat(fl)), hy - 3.0f - 2.0f * cr.rear, 1.0f, 1.0f);       /* the tongue */
        g_line(hx + f * 8.0f, hy - 1.0f - 2.0f * cr.rear, hx + f * (10.0f + 7.0f * sat(fl)), hy + 1.5f - 2.0f * cr.rear, 1.0f, 1.0f);
        for (i = 4; i < 16; i += 4)              /* and a few spines */
            g_line(xy[2 * i], xy[2 * i + 1] - 1.5f, xy[2 * i] - f * 2.0f, xy[2 * i + 1] - 6.5f, 1.2f, 0.9f);
        g_mode(G_PAINT);
        g_disc(hx + f * 3.0f, hy - 0.5f - 0.6f * cr.rear, 0.9f, 0.0f);
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
        ears = 0.0f;
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
            ears = 1.0f;
        }
    }
    for (i = 0; i < 12; i++)
        bud[i] += (sat(want[i] * 1.6f) - bud[i]) * minf(1.0f, dt * (want[i] > bud[i] ? 16.0f : 2.5f));
    cr_update(algo % 3, vis, ears >= 1.0f);
    ears = maxf(0.0f, ears - dt * 2.2f);

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
        f32 xy[2 * 31], amp = drone_vis * (1.2f + 3.0f * pagel);
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
        creature(algo % 3, gy, vis, pagel);
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
