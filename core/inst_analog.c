/* A22 - ANALOG: Thru-Zero Linear FM (TZFM) Virtual Analog + 24 dB Ladder.
 *
 * Band-limited PolyBLEP dual oscillators with PWM, true Thru-Zero Linear FM (phase runs
 * smoothly in reverse on negative instantaneous frequency -> stays in tune across all octaves!),
 * 5-osc stereo supersaw, hard sync, 303 acid, chord stacks, and a 24 dB/oct
 * Zero-Delay-Feedback non-linear saturated Ladder Filter driven by Mod-Env & Velocity.
 *
 *   K1 (BLUE)   SHAPE   pulse width / saw-pulse blend / chord interval / sync sweep
 *   K2 (GREEN)  TZFM    thru-zero linear FM depth / unison detune spread
 *   K3 (WHITE)  CUTOFF  24 dB analog ladder filter cutoff (Mod-Env + Velocity tracked)
 *   K4 (ORANGE) RESO    ladder resonance + warm pre-filter analog overdrive */
#include "internal.h"
#include "dsp.h"

static const f32 CHORD_SEMI[6][3] = {
    { 7.0f, 12.0f, 19.0f},   /* power / 5th */
    { 3.0f,  7.0f, 10.0f},   /* minor 7 */
    { 4.0f,  7.0f, 11.0f},   /* major 7 */
    { 5.0f,  7.0f, 12.0f},   /* sus4 */
    { 3.0f,  7.0f, 14.0f},   /* minor 9 */
    { 4.0f,  7.0f, 14.0f},   /* major 9 */
};

static struct {
    int algo;
    f32 shape, tzfm, cut, reso, drive, pwm_lfo;
} P;
static f32 pwm_ph, pulse_t_was;

static void pulse_block(const Blk *b)
{
    P.algo  = b->algo & 7;
    P.shape = b->k[0];
    P.tzfm  = b->k[1];
    P.cut   = 75.0f * exp2fx(b->k[2] * 7.6f);          /* 75 Hz .. 14.5 kHz */
    P.reso  = b->k[3] * 3.55f;
    P.drive = 1.0f + 2.4f * b->k[3] * b->k[3];
    if (b->time < pulse_t_was) pwm_ph = 0.0f;
    pulse_t_was = b->time;
    pwm_ph = fractf(pwm_ph + 0.45f * b->step);
    P.pwm_lfo = 0.08f * tsin(pwm_ph);
}

static void pulse_note(Voice *v, const Blk *b)
{
    int i;
    (void)b;
    if (v->env < 0.001f) {
        for (i = 0; i < 6; i++) v->ph[i] = fractf(v->r1 * (f32)(i + 1) * 0.29f);
        for (i = 0; i < 12; i++) v->s[i] = 0.0f;
    }
    v->s[8] = 1.0f;   /* punch pitch scoop */
}

static void pulse_render(Voice *v, const Blk *b, f32 *out_l, f32 *out_r, int n)
{
    int algo = P.algo, i;
    f32 pitch = v->pitch + b->bend;
    f32 vel = lerpf(1.0f, 0.35f + 0.85f * v->vel_f, b->vel_sens);
    f32 f, d0, d1, d2, d3, d4, fc;
    f32 pw = clampf(0.5f - 0.42f * P.shape + P.pwm_lfo, 0.06f, 0.94f);
    f32 det = 0.04f + 0.36f * P.tzfm;
    f32 p0 = v->ph[0], p1 = v->ph[1], p2 = v->ph[2], p3 = v->ph[3], p4 = v->ph[4];
    f32 tz_amt = P.tzfm * P.tzfm * 3.2f * lerpf(1.0f, 0.3f + 1.1f * v->menv, b->menv_amt);
    f32 comp = 1.0f / (1.0f + 0.45f * (P.drive - 1.0f));
    Ladder fl;

    if (algo == 3 || algo == 5) pitch -= 12.0f;        /* SUB / ACID sit an octave lower */
    if (algo == 6) pitch -= 0.45f * exp2fx(-v->age * 16.0f);  /* BRASS pitch scoop */
    if (b->punch > 0.01f) pitch += b->punch * v->s[8] * 1.4f;

    f = mtof(pitch);
    d0 = minf(f * b->isr, 0.42f);
    d1 = minf(d0 * exp2fx(det * (1.0f / 12.0f)), 0.42f);
    d2 = minf(d0 * exp2fx(-det * 0.9f * (1.0f / 12.0f)), 0.42f);
    d3 = minf(d0 * exp2fx(det * 1.85f * (1.0f / 12.0f)), 0.42f);
    d4 = minf(d0 * exp2fx(-det * 1.75f * (1.0f / 12.0f)), 0.42f);

    if (algo == 1) { /* TZFM modulator ratio on Osc B */
        static const f32 TRAT[5] = {0.5f, 1.0f, 1.5f, 2.0f, 3.0f};
        int ti = iclamp((int)(P.shape * 4.99f), 0, 4);
        d1 = minf(d0 * TRAT[ti], 0.42f);
    } else if (algo == 4) { /* SYNC slave pitch swept by shape + mod env */
        f32 sr_sync = 1.0f + (0.4f + 5.5f * P.shape) * (0.25f + 0.95f * v->menv);
        d1 = minf(d0 * sr_sync, 0.44f);
    } else if (algo == 7) { /* CHORD (stacked chord intervals) */
        int ci = iclamp((int)(P.shape * 5.99f), 0, 5);
        d1 = minf(d0 * exp2fx(CHORD_SEMI[ci][0] * (1.0f / 12.0f)), 0.42f);
        d2 = minf(d0 * exp2fx(CHORD_SEMI[ci][1] * (1.0f / 12.0f)), 0.42f);
        d3 = minf(d0 * exp2fx(CHORD_SEMI[ci][2] * (1.0f / 12.0f)), 0.42f);
    }

    /* Mod-Env + Velocity + Key-Tracking into 24 dB ZDF Ladder Filter */
    fc = P.cut * exp2fx(v->menv * b->menv_amt * (algo == 5 ? 5.2f : 3.6f)
                      + (v->vel_f - 0.5f) * b->vel_sens * 1.6f
                      + (pitch - 60.0f) * (1.0f / 36.0f));
    ladder_set(&fl, clampf(fc, 45.0f, 17000.0f), algo == 5 ? minf(3.75f, P.reso + 1.2f) : P.reso, b->isr);

    for (i = 0; i < n; i++) {
        f32 xl = 0.0f, xr = 0.0f;
        switch (algo) {
        case 0: /* DUAL: stereo detuned PolyBLEP Saw + PWM Pulse */
        case 6: /* BRASS */ {
            f32 s0 = saw_bl(p0, d0), s1 = saw_bl(p1, d1), sq = pulse_bl(p2, d2, pw);
            f32 mix = P.shape;
            xl = lerpf(s0, sq, mix * 0.65f) * 0.55f + s1 * 0.45f;
            xr = lerpf(s1, sq, mix * 0.65f) * 0.55f + s0 * 0.45f;
            break;
        }
        case 1: /* TZFM: True Thru-Zero Linear FM (carrier phase runs backward cleanly!) */ {
            f32 mod = tsin(p1);
            f32 df = d0 * (1.0f + tz_amt * mod);
            p0 += df;
            if (p0 >= 1.0f) p0 -= 1.0f;
            else if (p0 < 0.0f) p0 += 1.0f;
            p2 += d0 * 1.003f * (1.0f + tz_amt * 0.94f * mod);
            if (p2 >= 1.0f) p2 -= 1.0f;
            else if (p2 < 0.0f) p2 += 1.0f;
            xl = 0.65f * tsin(p0) + 0.35f * tri_wave(p0);
            xr = 0.65f * tsin(p2) + 0.35f * tri_wave(p2);
            break;
        }
        case 2: /* SUPER: 5-osc stereo supersaw / superpulse */ {
            f32 c0 = saw_bl(p0, d0);
            f32 a1 = saw_bl(p1, d1), a2 = saw_bl(p2, d2), a3 = saw_bl(p3, d3), a4 = saw_bl(p4, d4);
            f32 sq = pulse_bl(p0, d0, pw);
            f32 cen = lerpf(c0, sq, P.shape * 0.6f);
            xl = (cen * 0.45f + a1 * 0.35f + a3 * 0.25f + a2 * 0.15f) * 0.8f;
            xr = (cen * 0.45f + a2 * 0.35f + a4 * 0.25f + a1 * 0.15f) * 0.8f;
            break;
        }
        case 3: /* SUB: fat bass + clean sub-octave sine */ {
            f32 sub = tsin(p3);
            f32 main = lerpf(saw_bl(p0, d0), pulse_bl(p0, d0, pw), P.shape) * 0.62f + saw_bl(p1, d1) * 0.38f;
            xl = main * 0.7f + sub * (0.35f + 0.4f * P.tzfm);
            xr = xl;
            break;
        }
        case 4: /* SYNC: hard-synced slave sawtooth */ {
            f32 prev = p0;
            p0 += d0;
            if (p0 >= 1.0f) {
                p0 -= 1.0f;
                p1 = p0 * (d1 / maxf(1e-6f, d0));
                if (p1 >= 1.0f) p1 -= (f32)(int)p1;
            }
            (void)prev;
            xl = (saw_bl(p1, d1) * 0.72f + pulse_bl(p0, d0, 0.5f) * 0.35f) * 0.85f;
            xr = (saw_bl(p1, d1) * 0.65f + saw_bl(p2, d2) * 0.38f) * 0.85f;
            break;
        }
        case 5: /* ACID: 303 saw-to-square morph into screaming ladder */ {
            f32 x = lerpf(saw_bl(p0, d0), pulse_bl(p0, d0, 0.5f), P.shape);
            xl = xr = x * 0.92f;
            break;
        }
        default: /* 7 CHORD: harmonic chord stack in stereo */ {
            f32 c0 = saw_bl(p0, d0), c1 = saw_bl(p1, d1), c2 = saw_bl(p2, d2), c3 = saw_bl(p3, d3);
            xl = (c0 * 0.40f + c1 * 0.32f + c2 * 0.18f + c3 * 0.28f) * 0.85f;
            xr = (c0 * 0.40f + c1 * 0.18f + c2 * 0.32f + c3 * 0.28f) * 0.85f;
            break;
        }
        }

        /* 24 dB/oct ZDF Non-Linear Stereo Ladder Filter (+ subtle JP-8000 high-air shelf on SUPER) */
        {
            f32 rl = xl, rr = xr;
            xl = ladder_tick(&fl, &v->s[0], xl * P.drive) * comp * vel;
            xr = ladder_tick(&fl, &v->s[4], xr * P.drive) * comp * vel;
            if (algo == 2) {
                f32 air = 0.11f * P.tzfm * vel;
                xl += (rl - xl) * air;
                xr += (rr - xr) * air;
            }
        }
        out_l[i] = xl;
        out_r[i] = xr;

        if (algo != 1 && algo != 4) {
            p0 += d0; if (p0 >= 1.0f) p0 -= 1.0f;
        }
        if (algo != 4) {
            p1 += d1; if (p1 >= 1.0f) p1 -= 1.0f;
        } else {
            p1 += d1; if (p1 >= 1.0f) p1 -= 1.0f;
        }
        if (algo != 1) {
            p2 += d2; if (p2 >= 1.0f) p2 -= 1.0f;
        }
        p3 += (algo == 3 ? d0 * 0.5f : d3); if (p3 >= 1.0f) p3 -= 1.0f;
        p4 += d4; if (p4 >= 1.0f) p4 -= 1.0f;
    }

    v->ph[0] = p0; v->ph[1] = p1; v->ph[2] = p2; v->ph[3] = p3; v->ph[4] = p4;
    v->s[8] *= 1.0f - onepole_k(0.030f, b->sr / (f32)n);
}

/* ---- 4-Color Analog Graphic: Dual Oscilloscope + Shaded 24 dB Ladder Curve ------- */
static void pulse_draw(const Vis *vis)
{
    f32 shape = vis->k[0], tzfm = vis->k[1], cut = vis->k[2], reso = vis->k[3];
    f32 lev = minf(1.0f, vis->level * 3.0f);
    f32 xy[2 * 66];
    int algo = vis->algo & 7, i;

    /* Grid frame (Slate) */
    g_color(GC_SLATE);
    g_box(18.0f, 44.0f, 204.0f, 68.0f, 1.0f, 0.28f);
    g_box(18.0f, 124.0f, 204.0f, 76.0f, 1.0f, 0.28f);
    g_line(18.0f, 78.0f, 222.0f, 78.0f, 1.0f, 0.16f);
    g_line(120.0f, 44.0f, 120.0f, 112.0f, 1.0f, 0.12f);

    /* Top Box: Algorithm-Aware Oscillator Waveform (Blue = SHAPE) + Live Scope (Green = TZFM) */
    for (i = 0; i < 64; i++) {
        f32 u = (f32)i * (1.0f / 63.0f);
        f32 ph = fractf(u * 2.0f + vis->t * 0.35f);
        f32 w;
        if (algo == 1) {
            /* TZFM: thru-zero phase reversal */
            w = sin_turns(ph + tzfm * 0.85f * sin_turns(ph * (1.0f + (f32)(int)(shape * 3.0f))));
        } else if (algo == 4) {
            /* SYNC: hard-synced slave sawtooth */
            f32 sp = fractf(ph * (1.4f + 4.2f * shape));
            w = (2.0f * sp - 1.0f) * (1.0f - 0.35f * ph);
        } else if (algo == 2) {
            /* SUPER: multi-saw unison interference */
            w = 0.45f * (2.0f * ph - 1.0f)
              + 0.35f * (2.0f * fractf(ph * 1.02f + vis->t * 0.25f) - 1.0f)
              + 0.25f * (2.0f * fractf(ph * 0.98f - vis->t * 0.22f) - 1.0f);
        } else {
            w = lerpf(2.0f * ph - 1.0f, ph < (0.5f - 0.4f * shape) ? 1.0f : -1.0f, 0.35f + 0.65f * shape);
        }
        xy[2 * i]     = 22.0f + u * 196.0f;
        xy[2 * i + 1] = 78.0f - w * (18.0f + 8.0f * lev);
    }
    g_color(GC_BLUE);
    g_poly(xy, 64, 0, 1.8f, 0.78f);

    for (i = 0; i < 64; i++) {
        f32 u = (f32)i * (1.0f / 63.0f);
        f32 s = vis->scope[(vis->scope_pos + i * 5) & (SCOPE_N - 1)];
        f32 m = sin_turns(u * (2.0f + 5.0f * tzfm) + vis->t * (0.6f + 2.0f * tzfm)) * tzfm * 10.0f;
        xy[2 * i]     = 22.0f + u * 196.0f;
        xy[2 * i + 1] = 78.0f - clampf(s * 34.0f + m, -28.0f, 28.0f);
    }
    g_color(GC_GREEN);
    g_poly(xy, 64, 0, 2.2f, 0.94f);

    /* Bottom Box: Shaded 24 dB Ladder Filter Curve (White = CUTOFF, Orange = RESO peak) */
    {
        f32 cx_cut = 30.0f + cut * 174.0f;
        f32 peak_h = reso * (36.0f + 16.0f * lev);
        f32 py_peak = clampf(160.0f - peak_h, 128.0f, 196.0f);
        xy[0] = 22.0f;
        xy[1] = 196.0f;
        for (i = 0; i < 64; i++) {
            f32 x = 22.0f + (f32)i * (196.0f / 63.0f);
            f32 d = (x - cx_cut) * (1.0f / 22.0f);
            f32 pass = d < 0.0f ? 1.0f : 1.0f / (1.0f + d * d * d * d * 0.55f);
            f32 bump = peak_h / (1.0f + d * d * 4.5f);
            f32 y = 194.0f - pass * 34.0f - bump;
            xy[2 * (i + 1)]     = x;
            xy[2 * (i + 1) + 1] = clampf(y, 128.0f, 196.0f);
        }
        xy[2 * 65]     = 218.0f;
        xy[2 * 65 + 1] = 196.0f;

        /* Shaded vertical gradient fill under filter curve */
        g_color(GC_CYAN);
        g_fillv(xy, 66, 128.0f, 0.34f + 0.16f * lev, 196.0f, 0.03f);

        g_color(GC_WHITE);
        g_poly(xy + 2, 64, 0, 2.5f, 0.95f);

        /* Orange resonance glow, handle & vertical cutoff marker */
        g_color(GC_ORANGE);
        g_glow(cx_cut, py_peak, 12.0f + 16.0f * reso, 0.28f + 0.35f * reso);
        g_line(cx_cut, 196.0f, cx_cut, py_peak, 1.4f, 0.52f);
        g_disc(cx_cut, py_peak, 4.0f + 3.0f * reso, 0.96f);
        g_ring(cx_cut, py_peak, 7.5f + 3.5f * reso, 1.2f, 0.55f);
    }
    g_color(GC_WHITE);
}

const InstDef inst_analog = {
    "ANALOG",
    {238, 241, 245},
    {"SHAPE", "TZFM", "CUTOFF", "RESO"},
    {"DUAL", "TZFM", "SUPER", "SUB", "SYNC", "ACID", "BRASS", "CHORD"},
    {
        {35, 28, 62, 26},            /* K1..K4 defaults */
        0,                           /* algo: DUAL */
        {0, 48, 70, 32},             /* Amp ADSR */
        {62, 34, 65, 18},            /* Mod/Filter Env */
        {0, 40, 0, 2},               /* LFO */
        {0, 3, 1, 70},               /* Arp */
        {0, 50, 35}                  /* Mix */
    },
    0.42f,
    pulse_block, pulse_note, pulse_render, 0, pulse_draw, 0
};
