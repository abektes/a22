/* g_line against the plain way of drawing one: for every pixel near the line, its distance to the
 * line in floats. g_line steps that distance in integers and a path leaves out the round ends its
 * neighbouring strokes cover, and neither may move a pixel by more than one ink level (of a22's 32). Random
 * lines of every width, slope, place (on the screen, across its edge, far off it) and mode, then
 * random paths and arcs. Run by tests/run_tests.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/gfx.h"

#define N (FB_W * FB_H)
static uint8_t ref[N];
static int ref_mode, ref_level, ref_level5;
static uint32_t seed = 12345u;
static int fails;

static f32 rnd(void)                             /* 0..1 */
{
    seed = seed * 1664525u + 1013904223u;
    return (f32)(seed >> 8) / 16777216.0f;
}
static f32 span(f32 a, f32 b) { return a + (b - a) * rnd(); }

/* a22's pixel is a colour bank (bits 5..7, 0 here) and a level of 0..31; the modes as core/gfx.c ink()
 * has them for one bank */
static void ref_put(int x, int y, int cov)
{
    uint8_t *p;
    int v, lev5 = ref_level5;
    if ((unsigned)x >= FB_W || (unsigned)y >= FB_H || cov <= 0)
        return;
    if (cov > 256)
        cov = 256;
    p = &ref[y * FB_W + x];
    if (ref_mode == G_MAX) {
        v = (cov * lev5 + 128) >> 8;
        if (v > 31) v = 31;
        if (v > *p)
            *p = (uint8_t)v;
    } else if (ref_mode == G_ADD) {
        v = (cov * lev5 + 128) >> 8;
        v += *p;
        *p = (uint8_t)(v > 31 ? 31 : v);
    } else if (ref_level == 0) {
        *p = (uint8_t)(*p - ((*p * cov + 128) >> 8));
    } else if (cov >= 240) {
        *p = (uint8_t)lev5;
    } else {
        v = *p + (((lev5 - *p) * cov + 128) >> 8);
        *p = (uint8_t)(v > 31 ? 31 : (v < 0 ? 0 : v));
    }
}

/* every pixel of the screen that could be within reach, its distance to the segment */
static void ref_line(f32 x0, f32 y0, f32 x1, f32 y1, f32 w, f32 a)
{
    f32 dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy, R = w * 0.5f + 0.5f;
    int i, j, ia, ib, ja, jb;
    ref_level = a <= 0.0f ? 0 : (a >= 1.0f ? 255 : (int)(a * 255.0f + 0.5f));
    ref_level5 = a <= 0.0f ? 0 : (a >= 1.0f ? 31 : (int)(a * 31.0f + 0.5f));
    if (!ref_level && ref_mode != G_PAINT)
        return;
    ia = imax(0, ifloor(minf(x0, x1) - R - 2.0f));
    ib = imin(FB_W - 1, ifloor(maxf(x0, x1) + R + 2.0f));
    ja = imax(0, ifloor(minf(y0, y1) - R - 2.0f));
    jb = imin(FB_H - 1, ifloor(maxf(y0, y1) + R + 2.0f));
    for (j = ja; j <= jb; j++)
        for (i = ia; i <= ib; i++) {
            f32 px = (f32)i - x0, py = (f32)j - y0, t = (px * dx + py * dy) / len2, qx, qy, d, c;
            t = clampf(t, 0.0f, 1.0f);
            qx = px - t * dx;
            qy = py - t * dy;
            d = sqrtfx(qx * qx + qy * qy);
            c = R - d;
            ref_put(i, j, c <= 0.0f ? 0 : (c >= 1.0f ? 256 : (int)(c * 256.0f)));
        }
}

/* the most any pixel is off, and where */
static int worst(int *at)
{
    int i, m = 0;
    for (i = 0; i < N; i++) {
        int d = abs((int)g_fb[i] - (int)ref[i]);
        if (d > m) {
            m = d;
            *at = i;
        }
    }
    return m;
}

static void endpoints(int kind, f32 *p)
{
    f32 cx, cy, l, th;
    switch (kind % 6) {
    case 0:                                      /* anywhere on the screen */
        p[0] = span(0, 239); p[1] = span(0, 239); p[2] = span(0, 239); p[3] = span(0, 239);
        break;
    case 1:                                      /* short, any angle: what an outline is made of */
        cx = span(-4, 244); cy = span(-4, 244); l = span(0.05f, 9.0f); th = rnd();
        p[0] = cx; p[1] = cy; p[2] = cx + l * cos_turns(th); p[3] = cy + l * sin_turns(th);
        break;
    case 2:                                      /* across the edges and beyond */
        p[0] = span(-300, 540); p[1] = span(-300, 540); p[2] = span(-300, 540); p[3] = span(-300, 540);
        break;
    case 3:                                      /* on whole pixels, flat, upright and at 45 degrees */
        p[0] = (f32)(int)span(0, 239); p[1] = (f32)(int)span(0, 239);
        l = (f32)(int)span(1, 60);
        p[2] = p[0] + (seed & 256 ? l : 0.0f) * (seed & 1024 ? -1.0f : 1.0f);
        p[3] = p[1] + (seed & 512 ? l : 0.0f);
        if (p[2] == p[0] && p[3] == p[1])
            p[2] += l;
        break;
    case 4:                                      /* very far off, and very long: the float way inside g_line */
        p[0] = span(-6000, 6000); p[1] = span(-6000, 6000); p[2] = span(-200, 440); p[3] = span(-200, 440);
        break;
    default:                                     /* nearly flat and nearly upright */
        p[0] = span(0, 239); p[1] = span(0, 239);
        l = span(2, 200);
        if (seed & 256) { p[2] = p[0] + l; p[3] = p[1] + span(-0.02f, 0.02f) * l; }
        else { p[2] = p[0] + span(-0.02f, 0.02f) * l; p[3] = p[1] + l; }
        break;
    }
}

int main(void)
{
    int k, at = 0, d, lines = 0, paths = 0, arcs = 0;

    /* one line at a time, in every mode, over a ground that is not empty */
    for (k = 0; k < 60000; k++) {
        f32 p[4], w = k % 7 == 0 ? span(8.0f, 40.0f) : span(0.2f, 7.0f), a = k % 11 == 0 ? 1.0f : rnd(), ground = k % 3 ? 0.0f : span(0.1f, 0.9f);
        int mode = k % 5 == 3 ? G_ADD : (k % 5 == 4 ? G_PAINT : G_MAX);
        endpoints(k, p);
        if ((p[2] - p[0]) * (p[2] - p[0]) + (p[3] - p[1]) * (p[3] - p[1]) < 1e-3f)
            continue;
        g_clear();
        g_mode(G_MAX);
        if (ground > 0.0f)
            g_rect(0, 0, FB_W, FB_H, ground);
        memcpy(ref, g_fb, N);
        g_mode(mode);
        ref_mode = mode;
        g_line(p[0], p[1], p[2], p[3], w, a);
        ref_line(p[0], p[1], p[2], p[3], w, a);
        d = worst(&at);
        lines++;
        /* (G_PAINT takes a pixel whole from 240/256 of cover, core/gfx.c ink(): a cover one step under that,
         * which is as close as g_line's integer steps come to the float distance, lands two levels away) */
        if (d > (mode == G_PAINT ? 2 : 1)) {
            printf("FAIL line %d: (%g, %g) to (%g, %g), width %g, alpha %g, mode %d: pixel (%d, %d) is %d, should be %d\n",
                   k, (double)p[0], (double)p[1], (double)p[2], (double)p[3], (double)w, (double)a, mode, at % FB_W, at / FB_W, g_fb[at], ref[at]);
            if (++fails > 8)
                return 1;
        }
    }

    /* paths: g_poly against its strokes drawn whole, one after the other */
    for (k = 0; k < 6000; k++) {
        f32 xy[2 * 40], w = span(0.5f, 6.0f), a = span(0.2f, 1.0f), x = span(20, 220), y = span(20, 220), th = rnd();
        int n = 2 + (int)(rnd() * 38.0f), i, closed = k & 1, mode = k % 9 == 0 ? G_ADD : G_MAX;
        for (i = 0; i < n; i++) {
            f32 l = k % 4 == 0 && i % 5 == 0 ? 0.0f : span(0.5f, 14.0f);   /* (some strokes are dots) */
            xy[2 * i] = x;
            xy[2 * i + 1] = y;
            th += k % 3 ? span(-0.08f, 0.08f) : span(-0.45f, 0.45f);        /* smooth ones, and spiky ones */
            x += l * cos_turns(th);
            y += l * sin_turns(th);
        }
        g_clear();
        g_mode(mode);
        for (i = 0; i + 1 < n; i++)
            g_line(xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], w, a);
        if (closed && n > 2)
            g_line(xy[2 * n - 2], xy[2 * n - 1], xy[0], xy[1], w, a);
        memcpy(ref, g_fb, N);
        g_clear();
        g_poly(xy, n, closed, w, a);
        d = worst(&at);
        paths++;
        if (d > (mode == G_MAX ? 1 : 0)) {
            printf("FAIL path %d: %d points, closed %d, width %g, mode %d: pixel (%d, %d) is %d, should be %d\n",
                   k, n, closed, (double)w, mode, at % FB_W, at / FB_W, g_fb[at], ref[at]);
            if (++fails > 8)
                return 1;
        }
    }

    /* arcs: against a circle's worth of whole strokes */
    for (k = 0; k < 3000; k++) {
        f32 cx = span(40, 200), cy = span(40, 200), r = span(1.0f, 110.0f), t0 = rnd(), t1 = t0 + span(-1.0f, 1.0f), w = span(0.5f, 6.0f), a = span(0.2f, 1.0f);
        f32 sp = absf(t1 - t0), px, py;
        int n = iclamp((int)(sp * r * 0.9f) + 2, 2, 96), i;
        g_clear();
        g_mode(G_MAX);
        px = cx + r * cos_turns(t0);
        py = cy + r * sin_turns(t0);
        for (i = 1; i <= n; i++) {
            f32 t = t0 + (t1 - t0) * (f32)i / (f32)n, qx = cx + r * cos_turns(t), qy = cy + r * sin_turns(t);
            g_line(px, py, qx, qy, w, a);
            px = qx;
            py = qy;
        }
        memcpy(ref, g_fb, N);
        g_clear();
        g_arc(cx, cy, r, t0, t1, w, a);
        d = worst(&at);
        arcs++;
        if (d > 1) {
            printf("FAIL arc %d: centre (%g, %g), radius %g, %g to %g turns, width %g: pixel (%d, %d) is %d, should be %d\n",
                   k, (double)cx, (double)cy, (double)r, (double)t0, (double)t1, (double)w, at % FB_W, at / FB_W, g_fb[at], ref[at]);
            if (++fails > 8)
                return 1;
        }
    }

    printf("lines: %d lines, %d paths, %d arcs, %d failed\n", lines, paths, arcs, fails);
    return fails ? 1 : 0;
}
