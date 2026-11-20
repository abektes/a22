/* A22 - Studio DSP toolkit. Header-only, single-precision float, no libm. */
#pragma once
#include "mathx.h"

#define SIN_N 2048
extern f32 sin_tab[SIN_N + 1];
void dsp_init(void);

/* sin(2*pi*t) from table, t in turns, any finite value */
static inline f32 tsin(f32 t)
{
    f32 x;
    int32_t i;
    t -= (f32)ifloor(t);
    x = t * (f32)SIN_N;
    i = (int32_t)x;
    x -= (f32)i;
    i &= SIN_N - 1;
    return sin_tab[i] + (sin_tab[i + 1] - sin_tab[i]) * x;
}

static inline f32 tcos(f32 t) { return tsin(t + 0.25f); }

/* MIDI note to Hz (fractional notes fine) */
static inline f32 mtof(f32 n) { return 440.0f * exp2fx((n - 69.0f) * (1.0f / 12.0f)); }

/* polynomial band-limited step (PolyBLEP) */
static inline f32 blep(f32 t, f32 dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static inline f32 saw_bl(f32 ph, f32 dt) { return 2.0f * ph - 1.0f - blep(ph, dt); }

static inline f32 pulse_bl(f32 ph, f32 dt, f32 duty)
{
    f32 p2 = ph + 1.0f - duty;
    if (p2 >= 1.0f) p2 -= 1.0f;
    return (ph < duty ? 1.0f : -1.0f) + blep(ph, dt) - blep(p2, dt) - (2.0f * duty - 1.0f);
}

static inline f32 tri_wave(f32 ph) { return 4.0f * absf(ph - 0.5f) - 1.0f; }

/* asymmetric warm tape / tube saturation (adds musical 2nd + 3rd harmonics) */
static inline f32 tape_sat(f32 x)
{
    f32 y = softclip(x + 0.08f * x * x);
    return y - 0.075f;
}

/* smooth sine wavefolder */
static inline f32 fold_sin(f32 x) { return tsin(x * 0.25f); }

/* equal-power stereo pan law, pan in [-1, 1] */
static inline void pan_gains(f32 pan, f32 *gl, f32 *gr)
{
    f32 t = clampf(pan, -1.0f, 1.0f) * 0.125f + 0.125f;   /* 0 .. 0.25 turns */
    *gl = tcos(t);
    *gr = tsin(t);
}

/* ZDF Trapezoidal State-Variable Filter (12 dB/oct LP/BP/HP/Notch) */
typedef struct Svf { f32 g, k, a1, a2, a3; } Svf;
static inline void svf_set(Svf *f, f32 fc, f32 q, f32 isr)
{
    f32 x = fc * isr;
    if (x > 0.42f) x = 0.42f;
    if (x < 0.0002f) x = 0.0002f;
    f->g = tanfx(H1_PI * x);
    f->k = 1.0f / maxf(0.5f, q);
    f->a1 = 1.0f / (1.0f + f->g * (f->g + f->k));
    f->a2 = f->g * f->a1;
    f->a3 = f->g * f->a2;
}
#define SVF_TICK(f, s, x, lp, bp) do { \
        f32 v3_ = (x) - (s)[1]; \
        f32 v1_ = (f).a1 * (s)[0] + (f).a2 * v3_; \
        f32 v2_ = (s)[1] + (f).a2 * (s)[0] + (f).a3 * v3_; \
        (s)[0] = 2.0f * v1_ - (s)[0]; \
        (s)[1] = 2.0f * v2_ - (s)[1]; \
        (lp) = v2_; (bp) = v1_; \
    } while (0)

/* 24 dB/oct Zero-Delay-Feedback Non-Linear Analog Ladder Filter (Moog/Digitone style) */
typedef struct Ladder { f32 g, k, comp; } Ladder;
static inline void ladder_set(Ladder *f, f32 fc, f32 reso, f32 isr)
{
    f32 x = clampf(fc * isr, 0.00025f, 0.40f);
    f32 t = tanfx(H1_PI * x);
    f->g = t / (1.0f + t);
    f->k = clampf(reso, 0.0f, 3.85f);
    f->comp = 0.48f * f->k;
}
static inline f32 ladder_tick(const Ladder *f, f32 *s, f32 x)
{
    f32 G = f->g, G2 = G * G, G3 = G2 * G, G4 = G2 * G2;
    f32 S = G3 * s[0] + G2 * s[1] + G * s[2] + s[3];
    f32 u = (x * (1.0f + f->comp) - f->k * S) / (1.0f + f->k * G4);
    f32 v, y;
    u = softclip(u);
    v = (u - s[0]) * G; y = v + s[0]; s[0] = y + v;
    v = (y - s[1]) * G; y = v + s[1]; s[1] = y + v;
    v = (y - s[2]) * G; y = v + s[2]; s[2] = y + v;
    v = (y - s[3]) * G; y = v + s[3]; s[3] = y + v;
    return y;
}

/* white noise from a 32-bit xorshift state */
static inline f32 noise_s(uint32_t *s) { return (f32)(int32_t)rng_next(s) * (1.0f / 2147483648.0f); }

/* one-pole smoothing coefficient for a time constant in seconds */
static inline f32 onepole_k(f32 seconds, f32 rate)
{
    if (seconds <= 0.0f) return 1.0f;
    return 1.0f - exp2fx(-1.442695f / (seconds * rate));
}
