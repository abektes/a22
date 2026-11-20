/* ECHO sounds the same after hours on as it did at start. Its write counter counts up for as long
 * as the unit is on; the read position used to be that counter turned into a float, which kept
 * the fraction of a sample for about 13 minutes, then whole samples, then pairs, and the echo
 * went grainy until a restart (issue #31). Run by tests/run_tests.sh: the effect alone, fed the
 * same note at start and again after about 25 minutes of silence. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../core/internal.h"

void fx_init(f32 sr);
void fx_block(f32 *l, f32 *r, const InstParams *ip, f32 bpm);

#define HEAR (44100 / CTRL)                      /* one second of blocks */
static f32 take[2][HEAR * CTRL];

static void silence(InstParams *ip, long blocks)
{
    f32 l[CTRL], r[CTRL];
    long b;
    for (b = 0; b < blocks; b++) {
        memset(l, 0, sizeof l);
        memset(r, 0, sizeof r);
        fx_block(l, r, ip, 120.0f);
    }
}

static void listen(InstParams *ip, f32 *out)     /* a 50 ms note, then what the echo makes of it */
{
    f32 l[CTRL], r[CTRL];
    int b, n;
    for (b = 0; b < HEAR; b++) {
        for (n = 0; n < CTRL; n++) {
            int t = b * CTRL + n;
            l[n] = r[n] = t < 2205 ? 0.5f * sinf(6.2831853f * 440.0f * (f32)t / 44100.0f) : 0.0f;
        }
        fx_block(l, r, ip, 120.0f);
        for (n = 0; n < CTRL; n++)
            out[b * CTRL + n] = l[n] - (b * CTRL + n < 2205 ? 0.5f * sinf(6.2831853f * 440.0f * (f32)(b * CTRL + n) / 44100.0f) : 0.0f);
    }
}

int main(void)
{
    InstParams ip;
    double diff = 0.0, loud = 0.0;
    int i;
    memset(&ip, 0, sizeof ip);
    ip.fx[0] = 60;                               /* ECHO well up */
    ip.fx[1] = 3;                                /* half a beat: 5512.5 samples at half rate, a fraction */
    ip.fx[3] = 50;                               /* TONE in the middle: off */
    fx_init(44100.0f);
    silence(&ip, 4 * HEAR);                      /* the delay settles on its time */
    listen(&ip, take[0]);
    silence(&ip, 4 * HEAR);
    silence(&ip, (1L << 25) / (CTRL / 2));       /* the write counter past 2^25: about 25 minutes on */
    listen(&ip, take[1]);
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
