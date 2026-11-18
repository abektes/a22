/* h1 - a string: a delay line that feeds itself through a loss filter. TWANG and BARD's plucked
 * instruments are these, fed with recorded plucks (smp.h).
 *
 * The line is read a whole period back with four-point interpolation, which stays level almost to the
 * top of the band, so a string between two whole delays does not go dull for it. What it reads has a
 * part of its own slow average taken off (a delay line that feeds itself rings at 0 Hz as well as at the
 * note, and anything one-sided in the loop would pile up there; a plain DC blocker only moves that ring to
 * a few hertz, where a slow average a fortieth of the note down ends it), goes through a one-pole lowpass
 * (the high harmonics die first), and is scaled so the note lasts t60 seconds. The period is set to the
 * note with the filters' own delays taken off, so a dull string is as in tune as a bright one.
 *
 * tools/samples/dsp.py runs the same loop (string_run) and its inverse: a recorded note, run through the
 * inverse, leaves what this string has to be fed to give the recording back. Change one, change both. */
#pragma once
#include "internal.h"
#include "dsp.h"

#define STR_MASK (KS_LEN - 1)
#define STR_DCB 0.3f             /* how much of the slow average comes off */
#define STR_DCK 0.025f           /* the slow average's corner, against the note */

typedef struct Str {
    f32 h[4];                    /* the interpolation's four taps */
    uint32_t n;                  /* whole samples back to the first of them */
    f32 g, rho;                  /* the lowpass, and the gain round the loop */
    f32 k;                       /* the slow average's coefficient */
    f32 len;                     /* the line's length in samples (the picture and the pick use it) */
} Str;

/* atan(y / x) for x > 0, y >= 0, in radians */
static inline f32 str_atan(f32 y, f32 x)
{
    f32 r, r2, a;
    int flip = y > x;
    r = flip ? x / y : y / maxf(x, 1.0e-9f);
    r2 = r * r;
    a = r * (0.999866f + r2 * (-0.3302995f + r2 * (0.180141f + r2 * (-0.085133f + r2 * 0.0208351f))));
    return flip ? 1.5707963f - a : a;
}

/* A string sounding hz, its lowpass at g (0..1, 1 = none), dying in t60 seconds. */
static inline void str_set(Str *s, f32 hz, f32 g, f32 t60, f32 sr)
{
    f32 w, a = 1.0f - g, d, om, k;
    hz = clampf(hz, 8.0f, sr * 0.3f);
    while (sr / hz > (f32)(KS_LEN - 8))          /* too low for the line: an octave up (hz is at least 8, so this ends) */
        hz *= 2.0f;
    w = hz / sr;                                 /* turns a sample */
    om = H1_TAU * w;
    k = om * STR_DCK;
    d = 1.0f / w - str_atan(a * tsin(w), 1.0f - a * tsin(w + 0.25f)) / om + STR_DCB * k / (om * om + k * k);
    d = clampf(d, 3.0f, (f32)(KS_LEN - 5));
    s->len = d;
    s->n = (uint32_t)d - 1u;
    d -= (f32)s->n;                              /* 1..2: between the two middle taps */
    s->h[0] = -(d - 1.0f) * (d - 2.0f) * (d - 3.0f) * (1.0f / 6.0f);
    s->h[1] = d * (d - 2.0f) * (d - 3.0f) * 0.5f;
    s->h[2] = -d * (d - 1.0f) * (d - 3.0f) * 0.5f;
    s->h[3] = d * (d - 1.0f) * (d - 2.0f) * (1.0f / 6.0f);
    s->g = g;
    s->k = k;
    s->rho = exp2fx(-9.966f / (maxf(t60, 0.02f) * hz));
}

/* What comes back round the loop for the sample about to be written at line[w]. mem: two floats of the
 * string's own (the lowpass and the slow average). */
static inline f32 str_back(const f32 *line, uint32_t w, const Str *s, f32 *mem)
{
    uint32_t i = w - s->n;
    f32 y = s->h[0] * line[i & STR_MASK] + s->h[1] * line[(i - 1u) & STR_MASK]
          + s->h[2] * line[(i - 2u) & STR_MASK] + s->h[3] * line[(i - 3u) & STR_MASK];
    mem[1] += s->k * (y - mem[1]);
    mem[0] += s->g * (y - STR_DCB * mem[1] - mem[0]);
    return mem[0] * s->rho;
}
