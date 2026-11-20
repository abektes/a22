/* DELAY sounds the same after hours on as it did at start. Its write counter counts up for as long
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
