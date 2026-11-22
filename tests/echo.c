/* DELAY's right side echoes half as long again as the left, or three quarters where that would not fit
 * its line, at every TIME and tempo; never a few ms (it wrapped: 120 BPM 1/4 gave 7 ms on the right).
 * And DELAY sounds the same after hours on as it did at start. Its write counter counts up for as long
 * as the unit is on; in h1 the read position was that counter turned into a float, which kept the
 * fraction of a sample for about 13 minutes, then whole samples, then pairs, and the echo went
 * grainy until a restart (h1 issue #31). a22's DELAY (core/fx.c) reads from the integer counter with
 * the fraction kept apart; this keeps it that way. Run by tests/run_tests.sh: the effect alone, fed
 * the same note at start and again after about 25 minutes of silence. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../core/internal.h"

#define HEAR (44100 / CTRL)                      /* one second of blocks */
static f32 take[2][HEAR * CTRL];
static const uint8_t P[4] = {60, 3, 0, 0};       /* MIX well up, half a beat (5512.5 samples at half
                                                  * rate: a fraction), the least feedback, no tape wow */

static f32 first_echo(const f32 *o, int n)     /* ms to the first echo, -1 if none */
{
    int i;
    for (i = 0; i < n; i++)
        if (fabsf(o[i]) > 0.01f) return (f32)i * 1000.0f / 44100.0f;
    return -1.0f;
}

static int taps(void)                            /* every TIME, 40..240 BPM: the right tap against the left */
{
    static const f32 DIV[8] = {0.125f, 0.25f, 1.0f / 3.0f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
    static f32 ol[3 * 44100], orr[3 * 44100];
    int t, bpm, b, n, bad = 0, cases = 0;
    for (t = 0; t < 8; t++)
        for (bpm = 40; bpm <= 240; bpm += 10) {
            uint8_t p[4] = {80, 0, 0, 0};
            f32 in[CTRL], l, r, want_l;
            p[1] = (uint8_t)t;
            fx_reset();
            memset(in, 0, sizeof in);
            for (b = 0; b < 2 * HEAR; b++) {     /* two seconds: the times glide to where they are set */
                f32 a[CTRL], c[CTRL];
                memset(a, 0, sizeof a); memset(c, 0, sizeof c);
                fx_send_block(in, in, a, c, FX_DELAY, p, (f32)bpm, 1);
            }
            for (b = 0; b < 3 * HEAR; b++) {
                for (n = 0; n < CTRL; n++) in[n] = (b == 0 && n < 4) ? 0.9f : 0.0f;
                memset(ol + b * CTRL, 0, CTRL * sizeof(f32));
                memset(orr + b * CTRL, 0, CTRL * sizeof(f32));
                fx_send_block(in, in, ol + b * CTRL, orr + b * CTRL, FX_DELAY, p, (f32)bpm, 1);
            }
            l = first_echo(ol, 3 * HEAR * CTRL);
            r = first_echo(orr, 3 * HEAR * CTRL);
            want_l = DIV[t] * 60000.0f / (f32)bpm;
            while (want_l > 15200.0f * 2000.0f / 44100.0f) want_l *= 0.5f;
            cases++;
            if (l < 0.0f || r < 20.0f || fabsf(l - want_l) > 3.0f ||
                (fabsf(r / l - 1.5f) > 0.02f && fabsf(r / l - 0.75f) > 0.02f)) {
                if (bad++ < 8)
                    printf("FAIL echo: TIME %d at %d BPM: left %.1f ms (want %.1f), right %.1f ms\n", t, bpm, l, want_l, r);
            }
        }
    printf("echo: %d TIME x tempo cases, the right tap 1.5x or 0.75x the left in all but %d\n", cases, bad);
    return bad;
}

static void block(const f32 *in, f32 *out)
{
    f32 r[CTRL];
    memset(out, 0, CTRL * sizeof *out);
    memset(r, 0, sizeof r);
    fx_send_block(in, in, out, r, FX_DELAY, P, 120.0f, 1);
}

static void silence(long blocks)
{
    f32 in[CTRL], out[CTRL];
    long b;
    memset(in, 0, sizeof in);
    for (b = 0; b < blocks; b++)
        block(in, out);
}

static void listen(f32 *out)                     /* a 50 ms note, then what the echo makes of it */
{
    f32 in[CTRL];
    int b, n;
    for (b = 0; b < HEAR; b++) {
        for (n = 0; n < CTRL; n++) {
            int t = b * CTRL + n;
            in[n] = t < 2205 ? 0.5f * sinf(6.2831853f * 440.0f * (f32)t / 44100.0f) : 0.0f;
        }
        block(in, out + b * CTRL);
    }
}

int main(void)
{
    double diff = 0.0, loud = 0.0;
    int i;
    fx_init(44100.0f);
    if (taps())
        return 1;
    fx_reset();
    silence(4 * HEAR);                           /* the delay settles on its time */
    listen(take[0]);
    silence(4 * HEAR);
    silence((1L << 25) / (CTRL / 2));            /* the write counter past 2^25: about 25 minutes on */
    listen(take[1]);
    for (i = 0; i < HEAR * CTRL; i++) {
        double d = fabs((double)take[1][i] - (double)take[0][i]);
        if (d > diff) diff = d;
        loud += (double)take[0][i] * take[0][i];
    }
    if (loud < 1.0) {
        printf("FAIL echo: the echo made no sound to compare (energy %.3f)\n", loud);
        return 1;
    }
    if (diff > 1e-5) {
        printf("FAIL echo: after 25 minutes on, the echo differs from the first by up to %.5f\n", diff);
        return 1;
    }
    printf("echo: the same after 25 minutes on as at start (largest difference %.2g)\n", diff);
    return 0;
}
