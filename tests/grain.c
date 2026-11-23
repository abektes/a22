/* GRAIN, the ninth effect: a granular send, on DELAY's buffer. Run by tests/run_tests.sh. The effect alone
 * (fx_send_block), as tests/echo.c drives DELAY.
 *
 *   bounded      silence in is silence out, and noise in stays bounded (it peaks under 1.8 times what comes
 *                in where grains pile up; 4 is a runaway or not a number), with every knob at 0 and at 100
 *   pitch        a 440 Hz sine comes out at 440 Hz with PITCH centred, 880 at the top, 220 at the bottom
 *   no clicks    with SIZE at its top, PITCH at both ends and POSITION at both ends, no step in the output is
 *                a click, and the sine is still at its pitch, not held on one sample (the grains' windows close
 *                before their reads wrap or reach the write head)
 *   level        where the grains overlap, as loud as what goes in, within 4 dB; sparse, short grains are
 *                mostly gaps, so quieter, but no grain is louder than twice what came in
 *   freeze       held: the cloud goes on with nothing coming in, and what comes in is not heard; let go:
 *                silent again within a second once nothing comes in
 *   DENSITY 0    the effect does not run (as MIX at 0 for the others)
 *   asleep       0.74 s and more of silence in (its whole buffer zeros), it stops working, as PLATE does, and
 *                gives silence; sound coming in wakes it at once; frozen, it never sleeps
 *   the panel    FX held on the FX page with GRAIN freezes while it is held and keeps the page; a tap of FX
 *                still closes the page (on its release); with another effect FX closes it as it is pressed */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/internal.h"
#include "../core/dsp.h"
#include "../core/h1.h"

static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

#define SR 44100
#define BLOCKS(s) ((int)((s) * SR / CTRL))
static f32 outl[10 * SR], outr[10 * SR];
static uint32_t nrng = 12345u;

typedef enum { IN_SILENCE, IN_NOISE, IN_SINE } In;

/* runs GRAIN for s seconds with input `in` (a sine at hz), into outl/outr from `at`; returns samples written */
static int run(const uint8_t p[4], In in, f32 hz, f32 amp, f32 s, int at)
{
    static double ph;
    f32 x[CTRL], l[CTRL], r[CTRL];
    int b, n, nb = BLOCKS(s);
    for (b = 0; b < nb; b++) {
        for (n = 0; n < CTRL; n++) {
            if (in == IN_NOISE) {
                nrng = nrng * 1664525u + 1013904223u;
                x[n] = ((f32)(nrng >> 9) / 4194304.0f - 1.0f) * amp;
            } else if (in == IN_SINE) {
                x[n] = amp * (f32)sin(ph);
                ph += 6.283185307179586 * hz / SR;
                if (ph > 6.283185307179586) ph -= 6.283185307179586;
            } else {
                x[n] = 0.0f;
            }
        }
        memset(l, 0, sizeof l);
        memset(r, 0, sizeof r);
        fx_send_block(x, x, l, r, FX_GRAIN, p, 120.0f, 1);
        for (n = 0; n < CTRL; n++) {
            outl[at + b * CTRL + n] = l[n];
            outr[at + b * CTRL + n] = r[n];
        }
    }
    return nb * CTRL;
}

static double rms(int a, int n)
{
    double e = 0.0;
    int i;
    for (i = a; i < a + n; i++) e += 0.5 * ((double)outl[i] * outl[i] + (double)outr[i] * outr[i]);
    return sqrt(e / n);
}
static double peak(int a, int n)
{
    double m = 0.0;
    int i;
    for (i = a; i < a + n; i++) {
        if (fabs(outl[i]) > m) m = fabs(outl[i]);
        if (fabs(outr[i]) > m) m = fabs(outr[i]);
    }
    return m;
}
static double goertzel(int a, int n, double hz)    /* the energy at hz, left and right */
{
    double w = 2.0 * cos(6.283185307179586 * hz / SR), e = 0.0;
    int c, i;
    for (c = 0; c < 2; c++) {
        const f32 *o = c ? outr : outl;
        double s1 = 0.0, s2 = 0.0;
        for (i = a; i < a + n; i++) {
            double s0 = o[i] + w * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        e += s1 * s1 + s2 * s2 - w * s1 * s2;
    }
    return e;
}
static double steepest(int a, int n)              /* the largest step from one sample to the next */
{
    double m = 0.0;
    int i;
    for (i = a + 1; i < a + n; i++) {
        double d = fabs((double)outl[i] - outl[i - 1]), e = fabs((double)outr[i] - outr[i - 1]);
        if (d > m) m = d;
        if (e > m) m = e;
    }
    return m;
}

static void fresh(void)
{
    dsp_init();                                  /* (the sine table: h1_init fills it on the unit) */
    fx_init((f32)SR);
    fx_grain_freeze(0);
}

int main(void)
{
    char what[200];
    int i, k, n;

    {   /* bounded, at every corner of the four knobs */
        int quiet = 1, bounded = 1;
        for (i = 0; i < 16; i++) {
            uint8_t p[4];
            for (k = 0; k < 4; k++) p[k] = (i >> k) & 1 ? 100 : 0;
            if (p[2] == 0) p[2] = 1;             /* (DENSITY at 0 does not run: its least that runs) */
            fresh();
            n = run(p, IN_SILENCE, 0, 0, 0.5f, 0);
            if (peak(0, n) != 0.0) quiet = 0;
            fresh();
            n = run(p, IN_NOISE, 0, 0.9f, 2.0f, 0);
            if (getenv("GRAIN_PEAKS")) printf("  corner %3d %3d %3d %3d: peak %.2f\n", p[0], p[1], p[2], p[3], peak(0, n));
            if (!(peak(0, n) < 4.0)) { bounded = 0; printf("  corner %d %d %d %d: peak %.2f\n", p[0], p[1], p[2], p[3], peak(0, n)); }
        }
        CHECK(quiet, "silence in is silence out, at every corner of the knobs");
        CHECK(bounded, "noise in stays bounded, at every corner of the knobs");
    }

    {   /* pitch: POSITION 40, SIZE 50, DENSITY 70 */
        static const uint8_t PIT[3] = {50, 100, 0};
        static const double HZ[3] = {440.0, 880.0, 220.0};
        for (i = 0; i < 3; i++) {
            uint8_t p[4] = {40, 50, 70, 0};
            double at, others = 0.0;
            p[3] = PIT[i];
            fresh();
            n = run(p, IN_SINE, 440.0f, 0.5f, 3.0f, 0);
            at = goertzel(SR, n - SR, HZ[i]);
            for (k = 0; k < 3; k++) if (k != i) others += goertzel(SR, n - SR, HZ[k]);
            snprintf(what, sizeof what, "PITCH %d: 440 Hz comes out at %.0f Hz (%.3g there, %.3g at the other two)",
                     PIT[i], HZ[i], at, others);
            CHECK(at > 20.0 * others, what);
        }
    }

    {   /* no clicks: SIZE 100, DENSITY 60, PITCH and POSITION at both ends */
        int c;
        for (c = 0; c < 4; c++) {
            uint8_t p[4] = {0, 100, 60, 0};
            double pk, st, want, low;
            p[0] = c & 1 ? 100 : 0;
            p[3] = c & 2 ? 100 : 0;
            fresh();
            n = run(p, IN_SINE, 220.0f, 0.5f, 4.0f, 0);
            pk = peak(SR, n - SR);
            st = steepest(SR, n - SR);
            snprintf(what, sizeof what, "POSITION %d PITCH %d: no click (largest step %.4f, peak %.3f)", p[0], p[3], st, pk);
            CHECK(pk > 0.02 && st < 0.12 * pk, what);  /* (a 440 Hz sine's own step is 0.063 of its peak) */
            want = goertzel(SR, n - SR, p[3] ? 440.0 : 110.0);
            low = goertzel(SR, n - SR, 25.0);           /* (a read held on one sample: a slow swell) */
            snprintf(what, sizeof what, "POSITION %d PITCH %d: at its pitch (%.3g), not held (%.3g at 25 Hz)", p[0], p[3], want, low);
            CHECK(want > 10.0 * low, what);
        }
    }

    {   /* level: a 330 Hz sine at 0.25 (rms 0.177) */
        static const uint8_t DS[4][2] = {{20, 20}, {20, 90}, {90, 20}, {90, 90}};   /* DENSITY, SIZE */
        double in = 0.25 / sqrt(2.0);
        for (i = 0; i < 4; i++) {
            uint8_t p[4] = {30, 0, 0, 50};
            double db;
            p[2] = DS[i][0];
            p[1] = DS[i][1];
            fresh();
            n = run(p, IN_SINE, 330.0f, 0.25f, 6.0f, 0);
            db = 20.0 * log10(rms(SR, n - SR) / in);
            if (i == 0) {                        /* (sparse and short: about four 28 ms grains a second) */
                double pk = peak(SR, n - SR);
                snprintf(what, sizeof what, "DENSITY %d SIZE %d: %+.1f dB, peak %.3f against 0.25 in", p[2], p[1], db, pk);
                CHECK(db > -20.0 && pk < 0.5, what);
            } else {
                snprintf(what, sizeof what, "DENSITY %d SIZE %d: %+.1f dB of what goes in", p[2], p[1], db);
                CHECK(db > -4.0 && db < 4.0, what);
            }
        }
    }

    {   /* freeze */
        uint8_t p[4] = {40, 50, 70, 50};
        int a, b2, c2;
        double before, held, other, after;
        fresh();
        a = run(p, IN_SINE, 440.0f, 0.5f, 2.0f, 0);
        fx_grain_freeze(1);
        b2 = run(p, IN_SILENCE, 0, 0, 2.0f, a);
        c2 = run(p, IN_SINE, 1500.0f, 0.5f, 2.0f, a + b2);
        before = rms(a - SR, SR);
        held = rms(a + b2 - SR, SR);
        snprintf(what, sizeof what, "frozen, the cloud goes on with nothing coming in (%.3f, before %.3f)", held, before);
        CHECK(held > 0.4 * before, what);
        other = goertzel(a + b2 + SR / 2, c2 - SR / 2, 1500.0);
        snprintf(what, sizeof what, "frozen, what comes in is not heard (1500 Hz: %.3g, 440 Hz: %.3g)",
                 other, goertzel(a + b2 + SR / 2, c2 - SR / 2, 440.0));
        CHECK(other < 0.001 * goertzel(a + b2 + SR / 2, c2 - SR / 2, 440.0), what);
        fx_grain_freeze(0);
        n = run(p, IN_SILENCE, 0, 0, 2.0f, a + b2 + c2);
        after = peak(a + b2 + c2 + SR, n - SR);
        snprintf(what, sizeof what, "let go, silent within a second once nothing comes in (peak %.3g)", after);
        CHECK(after < 1e-6, what);
    }

    {   /* DENSITY 0: does not run */
        uint8_t p[4] = {40, 50, 0, 50};
        fresh();
        n = run(p, IN_NOISE, 0, 0.5f, 1.0f, 0);
        CHECK(peak(0, n) == 0.0, "DENSITY 0: nothing comes out");
    }

    {   /* asleep in silence */
        uint8_t p[4] = {40, 50, 70, 50};
        int a, b2;
        fresh();
        a = run(p, IN_SINE, 440.0f, 0.5f, 1.0f, 0);
        CHECK(!fx_grain_asleep(), "awake while sound comes in");
        b2 = run(p, IN_SILENCE, 0, 0, 0.6f, a);
        CHECK(!fx_grain_asleep(), "awake 0.6 s into silence (its buffer still holds sound)");
        b2 += run(p, IN_SILENCE, 0, 0, 0.4f, a + b2);
        CHECK(fx_grain_asleep(), "asleep after a second of silence");
        CHECK(peak(a + b2 - SR / 4, SR / 4) == 0.0, "and silent");
        n = run(p, IN_SINE, 440.0f, 0.5f, 0.5f, a + b2);
        CHECK(!fx_grain_asleep() && peak(a + b2, n) > 0.01, "sound coming in wakes it");
        fresh();
        run(p, IN_SINE, 440.0f, 0.5f, 1.0f, 0);
        fx_grain_freeze(1);
        n = run(p, IN_SILENCE, 0, 0, 2.0f, 0);
        CHECK(!fx_grain_asleep() && peak(SR, n - SR) > 0.01, "frozen, it never sleeps");
        fx_grain_freeze(0);
    }

    {   /* the panel */
        uint32_t t = 0;
        int f, fx_page;
        h1_init(SR);
        h1_button(H1_BTN_FX, 1); h1_frame(t += 20); h1_button(H1_BTN_FX, 0); h1_frame(t += 20);
        fx_page = h1_page();
        CHECK(fx_page != 0, "FX opens the FX page");
        st.fx_type = FX_GRAIN;
        st.fx_on = 1;
        h1_button(H1_BTN_FX, 1);
        for (f = 0; f < 20; f++) h1_frame(t += 20);              /* (400 ms) */
        CHECK(fx_grain_frozen(), "FX held on the FX page with GRAIN: frozen");
        CHECK(h1_page() == fx_page, "and the page stays");
        h1_button(H1_BTN_FX, 0); h1_frame(t += 20);
        CHECK(!fx_grain_frozen(), "let go: not frozen");
        CHECK(h1_page() == fx_page, "and the page still stays");
        h1_button(H1_BTN_FX, 1); h1_frame(t += 20);
        CHECK(!fx_grain_frozen() && h1_page() == fx_page, "a tap: not frozen as it is pressed");
        h1_button(H1_BTN_FX, 0); h1_frame(t += 20);
        CHECK(h1_page() == 0 && !fx_grain_frozen(), "a tap of FX closes the page, on its release");
        st.fx_type = FX_PLATE;
        h1_button(H1_BTN_FX, 1); h1_frame(t += 20); h1_button(H1_BTN_FX, 0); h1_frame(t += 20);
        h1_button(H1_BTN_FX, 1); h1_frame(t += 20);
        CHECK(h1_page() == 0, "with PLATE, FX closes the page as it is pressed, as before");
        h1_button(H1_BTN_FX, 0); h1_frame(t += 20);
    }

    printf("grain: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
