/* h1 - TIDE: the pad. Detuned saws through a slow filter, and a night sea under whatever the
 * algorithm hangs in the sky. The keyboard lies across the water: every note lifts a swell at its
 * own place and lights a star above it.
 *
 *   K1 GLOW    filter cutoff                           the moon climbs and fills
 *   K2 HALO    resonance                               rings around it
 *   K3 DRIFT   detune and wander                       the sea gets choppy
 *   K4 SURGE   slow swells of filter and level         the whole sea heaves
 */
#include "internal.h"
#include "dsp.h"

/* ---- sound ------------------------------------------------------------------------------ */
static struct {
    int algo;
    f32 cut, q, det, drift, surge;
} D;

static void tide_block(const Blk *b)
{
    D.algo = b->algo;
    D.cut = 70.0f * exp2fx(b->k[0] * 7.3f);
    D.q = 0.65f + b->k[1] * b->k[1] * 9.0f;
    D.det = 0.03f + 0.42f * b->k[2] * b->k[2];           /* semitones */
    D.drift = b->k[2];
    D.surge = b->k[3];
}

static void tide_note(Voice *v, const Blk *b)
{
    (void)b;
    if (!v->u[0]) v->u[0] = 0x51ED270Bu + v->idx * 3571u;
    if (v->env < 0.001f) {
        v->s[0] = v->s[1] = 0.0f;
        v->ph[4] = v->r1;
        v->ph[5] = v->r2;
    }
}

static void tide_render(Voice *v, const Blk *b, f32 *out, int n)
{
    Svf f;
    f32 pitch = v->pitch + b->bend, det = D.det, cut, oct = 0.0f, l1, l2, amp, bpmix = 0.0f, nz = 0.0f, sub = 0.0f, hi = 0.0f, duty = 0.5f;
    f32 da, db, dc, dd, pa = v->ph[0], pb = v->ph[1], pc = v->ph[2], pd = v->ph[3], q = D.q, shim = 0.0f;
    int i, algo = D.algo;

    /* each voice wanders a little by itself, and breathes on its own slow clock */
    v->s[3] += (rng_s(&v->u[0]) - v->s[3]) * 0.004f;
    v->s[2] += (v->s[3] - v->s[2]) * 0.01f;
    pitch += v->s[2] * D.drift * 2.2f;
    v->ph[4] = fractf(v->ph[4] + (0.07f + 0.5f * D.surge * D.surge) * (0.8f + 0.4f * v->r2) * (f32)n * b->isr);
    v->ph[5] = fractf(v->ph[5] + 0.31f * (f32)n * b->isr);
    l1 = tsin(v->ph[4]);
    l2 = tsin(v->ph[5]);
    oct = D.surge * 1.8f * l1 - 1.3f * exp2fx(-v->age * 1.6f);       /* the filter blooms open after the note starts */
    amp = 1.0f - 0.4f * D.surge * (0.5f - 0.5f * tsin(v->ph[4] + 0.2f));

    switch (algo) {
    default: sub = 0.5f; break;                                       /* MOON */
    case 1: hi = 0.3f; oct += 1.0f; break;                            /* SUN */
    case 2: bpmix = 0.6f; duty = 0.5f + 0.38f * l2; break;            /* ECLIPSE */
    case 3: shim = sat(v->age * 0.6f) * (0.5f + 0.5f * l2); oct += 0.5f * l2; break;   /* AURORA */
    case 4: nz = 0.3f; det *= 2.5f; q += 1.5f; v->s[5] += (rng_s(&v->u[0]) - v->s[5]) * 0.02f; oct += v->s[5] * 2.0f; break;   /* STORM */
    case 5: nz = 0.9f; bpmix = 0.7f; sub = 0.45f; oct += (pitch - 60.0f) * (1.0f / 14.0f); break;   /* FOG */
    case 6: pitch += 12.0f * exp2fx(-v->age * 8.0f); oct += 1.3f * exp2fx(-v->age * 1.6f) + 3.2f * exp2fx(-v->age * 2.6f); break;   /* COMET */
    case 7: pitch -= 12.0f; sub = 0.9f; oct -= 0.8f; break;           /* ABYSS */
    }
    if (algo == 5) amp *= 2.2f;
    cut = D.cut * exp2fx(oct + (pitch - 60.0f) * (1.0f / 30.0f));
    v->s[4] += (cut - v->s[4]) * 0.25f;
    svf_set(&f, v->s[4], q, b->isr);

    da = minf(mtof(pitch) * b->isr, 0.45f);
    db = minf(da * exp2fx(det * (1.0f / 12.0f)), 0.45f);
    dc = da * exp2fx(-det * 0.8f * (1.0f / 12.0f));
    dd = sub > 0.0f ? da * 0.5f : (shim > 0.0f ? minf(da * 3.0f, 0.45f) : minf(da * 2.0f, 0.45f));

    for (i = 0; i < n; i++) {
        f32 x, lo, bp;
        switch (algo) {
        case 2: x = 0.5f * (pulse_bl(pa, da, duty) + pulse_bl(pb, db, 1.0f - duty * 0.9f)); break;
        case 5: x = 0.12f * saw_bl(pa, da); break;
        default: x = 0.36f * (saw_bl(pa, da) + saw_bl(pb, db) + saw_bl(pc, dc)); break;
        }
        if (sub > 0.0f) x += sub * tsin(pd);
        else if (hi > 0.0f) x += hi * saw_bl(pd, dd);
        if (nz > 0.0f) x += nz * noise_s(&v->u[0]);
        SVF_TICK(f, &v->s[0], x, lo, bp);
        x = lo + (bp - lo) * bpmix;
        if (shim > 0.0f) x += 0.16f * shim * (tsin(pd) + 0.6f * tsin(pd * 2.0f));
        out[i] = x * amp;
        pa += da; if (pa >= 1.0f) pa -= 1.0f;
        pb += db; if (pb >= 1.0f) pb -= 1.0f;
        pc += dc; if (pc >= 1.0f) pc -= 1.0f;
        pd += dd; if (pd >= 1.0f) pd -= 1.0f;
    }
    v->ph[0] = pa; v->ph[1] = pb; v->ph[2] = pc; v->ph[3] = pd;
}

/* ---- picture ---------------------------------------------------------------------------- */
#define NLINE 9
#define NPT 44
#define NBG 12
static f32 swell[NVOICE], swx[NVOICE], lev, sea_ph[3], bolt_t, bolt_x, comet_u, comet_v, eye_x, eye_xv, eye_o, hz_y = 128.0f, hz_v;
static f32 gyre, hole_ey, hole_tw;
static uint32_t seen, bolt_seed;
static f32 bg[NBG][2];
static int bg_ready;

static f32 key_x(f32 pitch) { return 16.0f + clampf((pitch - 41.0f) / 50.0f, 0.0f, 1.0f) * 208.0f; }

/* ABYSS: the water near the hole is carried round it and drawn in. Works on a point of a wave. */
static void whirl(f32 *x, f32 *y)
{
    f32 nx = (*x - 120.0f) * (1.0f / 112.0f), ny = (*y - hole_ey) * (1.0f / 60.0f), d2 = nx * nx + ny * ny, w, c, s, k;
    if (d2 >= 1.0f)
        return;
    w = 1.0f - sqrtfx(d2);
    w *= w;
    c = cos_turns(hole_tw * w * 2.2f); s = sin_turns(hole_tw * w * 2.2f);
    k = 1.0f - 1.6f * hole_tw * w;
    *x = 120.0f + (nx * c - ny * s) * k * 112.0f;
    *y = hole_ey + (ny * c + nx * s) * k * 60.0f;
}

static void star(f32 x, f32 y, f32 s, f32 a)
{
    g_line(x - s, y, x + s, y, 1.6f, a);
    g_line(x, y - s, x, y + s, 1.6f, a);
    g_disc(x, y, s * 0.36f, a);
}

static f32 cloud_x(f32 mx, f32 t) { return (f32)ifloor(mx - 14.0f + 8.0f * sin_turns(t * 0.05f)); }

/* STORM's near rain: where drop i is across the sky (before the wind leans it) and how far down, 0..1 */
#define NFAR 18
#define NNEAR 12
static f32 rain_lean;
static void rain_drop(int i, f32 t, f32 cx, f32 *x, f32 *u)
{
    f32 h = fractf((f32)i * 0.618f + 0.27f);
    *u = fractf(h * 5.0f + t * (0.8f + 0.5f * h));
    *x = cx - 60.0f + fractf((f32)i * 0.381f + 0.5f) * 128.0f;
}

/* part of an ellipse as a polyline: angles in turns */
static void ell_arc(f32 cx, f32 cy, f32 rx, f32 ry, f32 t0, f32 t1, f32 w, f32 a)
{
    f32 xy[2 * 16];
    int i, n = iclamp((int)((t1 - t0) * 40.0f) + 3, 3, 16);
    if (a < 0.02f)
        return;
    for (i = 0; i < n; i++) {
        f32 an = lerpf(t0, t1, (f32)i / (f32)(n - 1));
        xy[2 * i] = cx + cos_turns(an) * rx;
        xy[2 * i + 1] = cy + sin_turns(an) * ry;
    }
    g_poly(xy, n, 0, w, a);
}

static void sky(int algo, f32 a, const Vis *vis, f32 mx, f32 my, f32 H)
{
    f32 glow = vis->k[0], halo = vis->k[1], t = vis->t, r = 29.0f + 4.0f * lev;
    int i, j;
    if (a <= 0.01f)
        return;
    /* HALO, for anything round */
    if (algo != 3 && algo != 4 && algo != 6)
        for (j = 0; j < 3; j++) {
            f32 ha = halo * (1.0f - 0.27f * (f32)j) * (0.45f + 0.55f * sat(lev * 2.0f + 0.3f * sin_turns(t * 0.4f + (f32)j * 0.2f))) * a;
            f32 rr = (algo == 7 ? 15.0f : r) + 9.0f + (f32)j * (9.0f + 5.0f * halo) + 2.0f * sin_turns(t * 0.7f - (f32)j * 0.17f);
            if (ha > 0.02f)
                g_ring(mx, my, rr, 1.6f, ha);
        }
    switch (algo) {
    default:                                     /* MOON: GLOW is its phase too */
        g_disc(mx, my, r, a);
        g_mode(G_PAINT);
        g_disc(mx - r * (0.42f + 1.7f * glow), my - r * 0.18f * (1.0f - glow), r * 1.02f, 0.0f);
        g_mode(G_MAX);
        break;
    case 1: {                                    /* SUN */
        f32 rs = r * 0.86f;
        g_disc(mx, my, rs, a);
        for (i = 0; i < 12; i++) {
            f32 an = (f32)i / 12.0f + t * 0.02f, c = cos_turns(an), s = sin_turns(an);
            f32 l0 = rs + 6.0f, l1 = l0 + 5.0f + 9.0f * glow + 16.0f * lev * (0.6f + 0.4f * sin_turns(t * 0.9f + (f32)i * 0.31f));
            g_line(mx + c * l0, my + s * l0, mx + c * l1, my + s * l1, 2.6f, a);
        }
        break;
    }
    case 2:                                      /* ECLIPSE: a hole with a corona */
        for (i = 0; i < 28; i++) {
            f32 an = (f32)i / 28.0f + t * 0.006f, c = cos_turns(an), s = sin_turns(an);
            f32 l1 = r + 4.0f + (6.0f + 22.0f * glow) * (0.45f + 0.55f * sin_turns(t * 0.23f + (f32)i * 0.618f)) + 14.0f * lev;
            g_line(mx + c * r, my + s * r, mx + c * l1, my + s * l1, 1.4f, 0.8f * a);
        }
        g_mode(G_PAINT);
        g_disc(mx, my, r, 0.0f);
        g_mode(G_MAX);
        g_ring(mx, my, r, 2.2f, a);
        g_disc(mx + r * cos_turns(0.62f - glow * 0.5f), my + r * sin_turns(0.62f - glow * 0.5f), 3.0f + 3.0f * glow, a);
        break;
    case 3:                                      /* AURORA: curtains */
        for (i = 0; i < 44; i++) {
            f32 x = -6.0f + (f32)i * 5.8f, w1 = sin_turns(x * 0.006f + t * 0.05f), w2 = sin_turns(x * 0.017f - t * 0.08f);
            f32 top = 26.0f + 16.0f * w1 + 8.0f * w2, len = (18.0f + 62.0f * glow) * (0.35f + 0.65f * (0.5f + 0.5f * sin_turns(x * 0.011f + t * 0.11f + w2 * 0.2f))) + 30.0f * lev;
            f32 al = (0.3f + 0.7f * (0.5f + 0.5f * sin_turns(x * 0.023f - t * 0.13f))) * a;
            g_line(x + 5.0f * w2, top, x + 9.0f * w2, minf(top + len, H - 4.0f), 2.0f, al);
        }
        break;
    case 4: {                                    /* STORM: a cloud, rain, and lightning where you play */
        f32 cy = (f32)ifloor(50.0f - 14.0f * glow), cx = cloud_x(mx, t), base = cy + 17.0f, flash = sat(bolt_t * 4.0f);     /* (on whole pixels: its shading holds still) */
        static const f32 PUFF[8][3] = {{-60, 7, 15}, {-38, -5, 23}, {-10, -15, 28}, {22, -12, 26}, {48, -2, 21}, {68, 9, 13}, {-16, 6, 22}, {20, 6, 22}};
        for (i = 0; i < NFAR; i++) {             /* the far rain: a grey veil under the cloud, leaning with the wind */
            f32 h = fractf((f32)i * 0.381f + 0.13f), u = fractf(h * 7.0f + t * (0.55f + 0.25f * h)), x0 = cx - 66.0f + fractf((f32)i * 0.618f) * 136.0f;
            f32 y = base + 2.0f + u * (H - base - 12.0f);
            g_beam(x0 - rain_lean * (y - base), y, x0 - rain_lean * (y + 11.0f - base), y + 11.0f, 1.0f, 1.2f, 0.0f, 0.34f * a * sat(u * 5.0f), 0.0f);
        }
        if (bolt_t > 0.0f) {
            f32 xy[2 * 7], bx = bolt_x;
            uint32_t s = bolt_seed;
            for (i = 0; i < 7; i++) {
                xy[2 * i] = bx + (i == 0 || i == 6 ? 0.0f : rng_s(&s) * 15.0f);
                xy[2 * i + 1] = cy + 16.0f + (H - cy - 16.0f) * (f32)i / 6.0f;
            }
            g_glow(bx, H - 2.0f, 30.0f, flash * 0.5f * a);
            g_poly(xy, 7, 0, 2.6f, sat(bolt_t * 5.0f) * a);
        }
        for (i = 0; i < 8; i++)                  /* the cloud: its billows, */
            g_disc(cx + PUFF[i][0], cy + PUFF[i][1], PUFF[i][2], (0.4f + 0.3f * glow + 0.3f * flash) * a);
        g_mode(G_PAINT);                         /* its belly, heavy and dark, */
        for (i = 0; i < 5; i++)
            g_glow(cx - 62.0f + (f32)i * 33.0f, base + 9.0f, 34.0f, (0.93f - 0.5f * flash) * a);
        g_mode(G_MAX);
        for (i = 0; i < 6; i++)                  /* and the light along the top of each billow */
            g_arc(cx + PUFF[i][0], cy + PUFF[i][1], PUFF[i][2] - 0.7f, 0.57f + 0.02f * (f32)i, 0.9f + 0.012f * (f32)i, 1.5f, (0.8f + 0.2f * flash) * a);
        if (flash > 0.0f)                        /* (the stroke lights it from inside) */
            g_glow(clampf(bolt_x, cx - 50.0f, cx + 56.0f), cy + 4.0f, 30.0f, flash * 0.8f * a);
        for (i = 0; i < NNEAR; i++) {            /* the near rain: each drop a streak with a bright head */
            f32 x, y, u, len = 13.0f + 9.0f * fractf((f32)i * 0.731f);
            rain_drop(i, t, cx, &x, &u);
            y = base + 4.0f + u * (H - base - 4.0f);
            g_beam(x - rain_lean * (y - len - base), maxf(y - len, base), x - rain_lean * (y - base), y, 1.0f, 1.9f, 0.0f, 0.85f * a * sat(u * 6.0f), 0.0f);
        }
        break;
    }
    case 5:                                      /* FOG: a moon behind slow bands */
        g_disc(mx, my, r * 1.08f, (0.35f + 0.6f * glow) * a);
        g_mode(G_PAINT);
        for (i = 0; i < 5; i++) {
            f32 by = my - r + fractf((f32)i * 0.2f + t * 0.012f) * 2.0f * r;
            g_rect(mx - r - 6.0f, by - 2.5f, 2.0f * r + 12.0f, 4.5f + 2.0f * sin_turns(t * 0.1f + (f32)i * 0.3f), 0.0f);
        }
        g_mode(G_MAX);
        for (i = 0; i < 7; i++) {
            f32 fy = 22.0f + (f32)i * 15.0f + 4.0f * sin_turns(t * 0.03f + (f32)i * 0.4f), fx = fractf((f32)i * 0.37f + t * 0.008f * (f32)(1 + i % 3)) * 380.0f - 110.0f;
            g_line(fx, fy, fx + 70.0f + 14.0f * (f32)(i % 4), fy, 2.2f, 0.4f * a);
        }
        break;
    case 6: {                                    /* COMET: each note throws it further round */
        f32 an = 0.5f + comet_u * 0.5f, cx = 120.0f + 150.0f * cos_turns(an), cy = H + 10.0f + 118.0f * sin_turns(an);
        f32 tx = sin_turns(an), ty = -cos_turns(an);
        g_comet(cx, cy, 5.0f + 2.0f * lev, tx, ty, 46.0f + 90.0f * glow + 50.0f * lev, 10.0f + 16.0f * glow, a, t, 0.61f);
        break;
    }
    case 7:                                      /* ABYSS: a small moon far up; the rest is underwater */
        g_disc(mx, my, 15.0f, a);
        break;
    }
}

static void tide_draw(const Vis *vis)
{
    f32 dt = vis->dt, t = vis->t, glow = vis->k[0], drift = vis->k[2], surge = vis->k[3], mt = smooth01(vis->algo_t);
    f32 H, mx = 152.0f, my, big, ab, want[NVOICE] = {0};
    int i, j, k;

    if (!bg_ready) {
        uint32_t s = 0xC0FFEE11u;
        for (i = 0; i < NBG; i++) {
            bg[i][0] = rng_f(&s) * 240.0f;
            bg[i][1] = rng_f(&s);
        }
        bg_ready = 1;
    }
    if (vis->fresh) {
        seen = vis->notes;
        lev = 0.0f;
        bolt_t = 0.0f;
        hz_y = vis->algo == 7 ? 56.0f : 128.0f;
        hz_v = 0.0f;
        for (i = 0; i < NVOICE; i++)
            swell[i] = 0.0f;
    }
    lev += (minf(1.0f, vis->level * 3.5f) - lev) * minf(1.0f, dt * 6.0f);
    spring(&hz_y, &hz_v, vis->algo == 7 ? 56.0f : 128.0f, 7.0f, dt);
    H = hz_y;
    my = vis->algo == 7 ? H - 22.0f - glow * 14.0f : H + 16.0f - glow * 98.0f;
    if (vis->notes != seen) {
        seen = vis->notes;
        bolt_t = 0.3f;
        bolt_x = key_x(vis->last_pitch);
        bolt_seed = rng_next(vis->rng) | 1u;
        comet_v += 0.22f;
    }
    bolt_t = maxf(0.0f, bolt_t - dt);
    comet_v -= comet_v * minf(1.0f, dt * 2.5f);
    comet_u = fractf(comet_u + dt * (0.012f + comet_v));
    for (i = 0; i < NVOICE; i++) {
        if (vis->v[i].on) {
            want[i] = vis->v[i].env;
            swx[i] = key_x(vis->v[i].pitch);
        }
        swell[i] += (want[i] - swell[i]) * minf(1.0f, dt * (want[i] > swell[i] ? 5.0f : 2.2f));
    }
    rain_lean = 0.26f + 0.1f * sin_turns(t * 0.06f) + 0.25f * surge;
    for (i = 0; i < 3; i++)
        sea_ph[i] = fractf(sea_ph[i] + dt * (0.05f + 0.07f * (f32)i) * (1.0f + 1.5f * surge));

    /* the sky */
    for (i = 0; i < NBG; i++) {
        f32 y = 8.0f + bg[i][1] * (H - 30.0f), tw = 0.5f + 0.5f * sin_turns(t * (0.2f + 0.05f * (f32)(i % 4)) + bg[i][1] * 3.0f);
        if (y < H - 8.0f)
            g_disc(bg[i][0], y, 1.1f, 0.2f + 0.3f * tw);
    }
    if (mt < 1.0f)
        sky(vis->algo_prev, 1.0f - mt, vis, mx, my, H);
    sky(vis->algo, mt, vis, mx, my, H);
    for (i = 0; i < NVOICE; i++)                 /* a star for every sounding note, above its swell */
        if (swell[i] > 0.02f) {
            f32 sy = 14.0f + fractf((f32)i * 0.618f + 0.1f) * (H - 50.0f);
            star(swx[i], sy, (3.0f + 6.0f * swell[i]) * (0.85f + 0.15f * sin_turns(t * 1.3f + (f32)i * 0.2f)), sat(swell[i] * 3.0f));
        }

    /* everything below the horizon is water */
    g_mode(G_PAINT);
    g_rect(-1.0f, H, 242.0f, 242.0f - H, 0.0f);
    g_mode(G_MAX);

    ab = vis->algo == 7 ? mt : (vis->algo_prev == 7 ? 1.0f - mt : 0.0f);
    if (ab > 0.0f) {                             /* ABYSS: the sea turns round a hole, and what lives in it looks up when you play */
        spring(&eye_x, &eye_xv, (key_x(vis->last_pitch) - 120.0f) * 0.2f, 6.0f, dt);
        eye_o += ((0.06f + 0.94f * sat(lev * 1.6f)) - eye_o) * minf(1.0f, dt * 5.0f);
        gyre = fractf(gyre + dt * (0.05f + 0.12f * surge + 0.2f * lev));
        hole_ey = H + 104.0f;
        hole_tw = ab * (0.2f + 0.16f * sat(lev * 1.5f) + 0.14f * surge + 0.03f * sin_turns(t * 0.11f));
    }

    /* the sea: lines that come nearer and bigger, bleeding off both sides */
    big = 0.35f + 0.9f * surge + 0.45f * lev;
    for (k = 0; k < NLINE; k++) {
        f32 u = (f32)k / (f32)(NLINE - 1), y0 = H + 5.0f + u * sqrtfx(u) * (236.0f - H), amp = (1.0f + 7.0f * u) * big, xy[2 * NPT];
        f32 wl = 1.0f / (46.0f + 70.0f * u), dir = (k & 1) ? 1.0f : -1.0f, heave = 5.0f * surge * u * sin_turns(sea_ph[0] * 0.5f + u * 0.2f);
        for (j = 0; j < NPT; j++) {
            f32 x = -9.0f + (f32)j * 6.0f, y;
            y = amp * (sin_turns(x * wl + dir * sea_ph[0] + u * 1.7f) + 0.5f * sin_turns(x * wl * 1.9f - dir * sea_ph[1] + u * 0.6f));
            if (drift > 0.01f)
                y += amp * drift * (0.45f * sin_turns(x * wl * 2.7f + sea_ph[2] * 3.0f + u * 2.0f) + 0.3f * sin_turns(x * wl * 4.1f - sea_ph[1] * 4.0f + u));
            for (i = 0; i < NVOICE; i++)
                if (swell[i] > 0.01f) {
                    f32 d = (x - swx[i]) / (16.0f + 22.0f * u);
                    if (d > -2.0f && d < 2.0f) {
                        d = 1.0f - d * d * 0.25f;
                        y -= swell[i] * (4.0f + 13.0f * u) * d * d;
                    }
                }
            xy[2 * j] = x;
            xy[2 * j + 1] = y0 + y + heave;
        }
        if (ab > 0.0f && y0 > hole_ey - 70.0f) { /* ABYSS: the line is bent round the hole, with a point more between two where the bend is tight */
            static f32 wxy[2 * (2 * NPT - 1)];
            int n = 0;
            for (j = 0; j < NPT; j++) {
                f32 nx = (xy[2 * j] - 120.0f) * (1.0f / 112.0f), ny = (xy[2 * j + 1] - hole_ey) * (1.0f / 60.0f);
                if (j && nx * nx + ny * ny < 0.5f) {
                    wxy[2 * n] = 0.5f * (xy[2 * j] + xy[2 * j - 2]);
                    wxy[2 * n + 1] = 0.5f * (xy[2 * j + 1] + xy[2 * j - 1]);
                    whirl(&wxy[2 * n], &wxy[2 * n + 1]);
                    n++;
                }
                wxy[2 * n] = xy[2 * j];
                wxy[2 * n + 1] = xy[2 * j + 1];
                whirl(&wxy[2 * n], &wxy[2 * n + 1]);
                n++;
            }
            g_poly(wxy, n, 0, 1.3f + 2.0f * u, 0.3f + 0.7f * u);
        } else
            g_poly(xy, NPT, 0, 1.3f + 2.0f * u, 0.3f + 0.7f * u);
        /* the light on the water: under the moon the swell itself lights up. The lit stretch of
         * each wave is broken into glints by two slow ripples crossing, so the path shivers
         * instead of blinking, and it widens towards you. */
        if (vis->algo != 3 && vis->algo != 4 && vis->algo != 6 && vis->algo != 7 && my < H - 4.0f) {
            f32 half = (4.0f + 34.0f * u * (0.55f + 0.45f * u)) * (0.6f + 0.8f * glow), gl = (0.35f + 0.65f * glow) * mt;
            f32 cx = mx + 4.0f * u * sin_turns(t * 0.13f + (f32)k * 0.29f), px = 0.0f, py = 0.0f, pa = 0.0f, x;
            f32 r1 = 0.022f + 0.003f * (f32)k, r2 = 0.057f - 0.002f * (f32)k, lw2 = 2.9f + 3.3f * u;
            for (x = cx - half; x <= cx + half + 0.5f; x += 2.5f) {
                f32 fx = (x + 9.0f) * (1.0f / 6.0f), d = (x - cx) / half, yy, m, ga;
                int j0 = iclamp((int)fx, 0, NPT - 2);
                yy = lerpf(xy[2 * j0 + 1], xy[2 * j0 + 3], sat(fx - (f32)j0));
                m = sin_turns(x * r1 + t * 0.21f * dir + (f32)k * 0.37f) + sin_turns(x * r2 - t * 0.34f * dir + (f32)k * 0.61f);
                m = smooth01(m * 0.9f + 0.25f);
                ga = gl * (1.0f - d * d) * m;
                if (pa > 0.04f && ga > 0.04f)
                    g_line(px, py - 0.5f, x, yy - 0.5f, lw2 * (0.55f + 0.45f * sat(0.5f * (pa + ga) * 1.5f)), sat(0.75f * (pa + ga)));
                px = x; py = yy; pa = ga;
            }
            if (k == 0)                          /* where the path meets the horizon it is one bright seam */
                g_line(mx - 3.0f - 5.0f * glow, H + 2.0f, mx + 3.0f + 5.0f * glow, H + 2.0f, 1.4f, gl * 0.9f);
        }
    }

    if (vis->algo == 4 || (mt < 1.0f && vis->algo_prev == 4)) {      /* STORM: where the near rain lands, a ring on the water */
        f32 a = vis->algo == 4 ? mt : 1.0f - mt, cx = cloud_x(mx, t), base = 67.0f - 14.0f * glow;
        for (i = 0; i < NNEAR; i++) {
            f32 x, u, d = fractf((f32)i * 0.447f + 0.2f);
            rain_drop(i, t, cx, &x, &u);
            if (u < 0.3f) {
                f32 g = u * (1.0f / 0.3f), sx = x - rain_lean * (H - base), sy = H + 5.0f + 44.0f * d * d;
                g_ellipse(sx, sy, (2.0f + 9.0f * g) * (0.6f + d), (0.7f + 2.2f * g) * (0.6f + d), 1.0f, a * (1.0f - g) * (0.5f + 0.4f * d));
                if (g < 0.4f)
                    g_line(sx, sy, sx, sy - 4.0f * (1.0f - g * 2.5f) * (0.6f + d), 1.2f, a * 0.8f);
            }
        }
    }

    if (ab > 0.0f) {                             /* ABYSS: the hole itself, over the sea that pours into it */
        f32 ex = 120.0f, ey = hole_ey, rx = (54.0f + 6.0f * lev) * ab, ry = rx * 0.47f, lid, xy[2 * 13], ga = (0.55f + 0.45f * glow) * ab;
        for (k = 0; k < 3; k++)                  /* foam drawn out along the turning water */
            for (j = 0; j < 1; j++) {
                f32 a0 = gyre + (f32)k / 3.0f + (f32)j * 0.13f, r0 = 1.12f + 0.2f * (f32)j;
                for (i = 0; i < 9; i++) {
                    f32 q = (f32)i / 8.0f, an = a0 - q * 0.3f, rr = r0 + q * (0.55f + 0.2f * (f32)j);
                    xy[2 * i] = ex + cos_turns(an) * rx * rr;
                    xy[2 * i + 1] = ey + sin_turns(an) * ry * rr;
                }
                g_poly(xy, 5, 0, 2.0f - 0.6f * (f32)j, ga * (0.8f - 0.3f * (f32)j));
                g_poly(xy + 8, 5, 0, 1.2f, ga * (0.4f - 0.15f * (f32)j));
            }
        g_mode(G_PAINT);
        g_ellipse(ex, ey, rx, ry, 0.0f, 0.0f);
        g_mode(G_MAX);
        for (k = 0; k < 3; k++) {                /* the walls of it going down: rings, each lower, smaller and turning faster */
            f32 q = (f32)k / 4.0f, kx = rx * (1.0f - 0.2f * q * (2.0f - q) * 2.0f), ky = ry * (1.0f - 0.2f * q * (2.0f - q) * 2.0f), cy = ey + ry * 0.42f * q;
            if (k == 0) {
                g_ellipse(ex, cy, kx, ky, 2.4f + 1.6f * lev, ab);
                continue;
            }
            for (j = 0; j < 2; j++) {
                f32 a0 = gyre * (1.0f + (f32)k) + (f32)j / 2.0f + (f32)k * 0.17f;
                ell_arc(ex, cy, kx, ky, a0, a0 + 0.3f - 0.04f * (f32)k, 1.5f - 0.2f * (f32)k, ga * (0.75f - 0.17f * (f32)k));
            }
        }
        for (i = 0; i < 6; i++) {                /* whatever was floating goes round, and in */
            f32 h = fractf((f32)i * 0.618f), q = fractf(h + t * (0.05f + 0.03f * h)), rr = 2.1f - 1.5f * q, an = h + gyre * 0.5f + q * q * 1.6f;
            f32 px = ex + cos_turns(an) * rx * rr, py = ey + sin_turns(an) * ry * rr;
            if (rr > 1.02f)
                g_line(px, py, px + sin_turns(an) * 4.0f * q, py - cos_turns(an) * 2.0f * q, 1.4f, ga * sat(q * 4.0f));
        }
        /* the eye, at the bottom */
        lid = ry * 0.56f * eye_o;
        {
            f32 cy = ey + ry * 0.28f, lw = rx * 0.6f, ir = minf(lid * 0.92f, rx * 0.2f), px = ex + eye_x * ab;
            g_glow(ex, cy, rx * 0.5f, 0.25f * ga * eye_o);
            for (j = -1; j <= 1; j += 2) {       /* two lids */
                for (i = 0; i < 13; i++) {
                    f32 q = (f32)i / 12.0f * 2.0f - 1.0f;
                    xy[2 * i] = ex + q * lw;
                    xy[2 * i + 1] = cy + (f32)j * maxf(lid, 1.0f) * (1.0f - q * q);
                }
                g_poly(xy, 13, 0, 1.8f, ga);
            }
            if (ir > 2.5f) {
                g_disc(px, cy, ir, ga);
                g_mode(G_PAINT);
                for (i = 0; i < 10; i++) {       /* the iris has a grain */
                    f32 an = (f32)i / 10.0f + 0.03f * sin_turns(t * 0.2f + (f32)i);
                    g_line(px + cos_turns(an) * ir * 0.45f, cy + sin_turns(an) * ir * 0.45f, px + cos_turns(an) * ir * 0.86f, cy + sin_turns(an) * ir * 0.86f, 1.0f, 0.0f);
                }
                g_ellipse(px, cy, ir * (0.2f + 0.3f * (1.0f - sat(lev * 1.5f))), ir * 0.62f, 0.0f, 0.0f);       /* a slit that narrows at the sound */
                g_mode(G_MAX);
                g_ring(px, cy, ir + 2.2f, 1.0f, ga * 0.6f);
                g_disc(px - ir * 0.38f, cy - ir * 0.4f, 1.3f, ga);
            }
        }
    }
}

const InstDef inst_tide = {
    "TIDE",
    {128, 212, 255},
    {"GLOW", "HALO", "DRIFT", "SURGE"},
    {"MOON", "SUN", "ECLIPSE", "AURORA", "STORM", "FOG", "COMET", "ABYSS"},
    {{55, 25, 35, 30}, 0, {55, 60, 80, 62}, {0, 55, 0, 0}, {20, 4, 45, 50}, {0, 3, 1, 60}, {0, 0, 0}},
    0.3f,
    tide_block, tide_note, tide_render, 0, tide_draw,
};
