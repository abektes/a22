/* A22 - SHAPER: Casio CZ-style Phase Distortion + 2x Buchla Wavefolder.
 *
 * Crisp modern West-Coast / digital synthesis: Casio CZ-style phase-transfer distortion
 * and windowed resonant sync fed into a 2x oversampled asymmetric Buchla trigonometric
 * wavefolder with exact DC-bias cancellation, stereo detuned carrier pair, and warm ZDF LPF.
 *
 *   K1 (BLUE)   WARP  phase distortion kink / resonant sync peak / digital warp
 *   K2 (GREEN)  FOLD  2x oversampled Buchla wavefolder drive (Mod-Env + Velocity tracked)
 *   K3 (WHITE)  BIAS  asymmetric folder bias (even/odd harmonic balance) + stereo detune
 *   K4 (ORANGE) TONE  per-voice resonant ZDF low-pass filter */
#include "internal.h"
#include "dsp.h"

/* Casio CZ piecewise linear phase distortion transfer (warp in 0..1) */
static inline f32 pd_warp(f32 p, f32 warp)
{
    f32 d = clampf(0.5f - 0.46f * warp, 0.04f, 0.5f);
    if (p < d) return 0.5f * (p / d);
    return 0.5f + 0.5f * ((p - d) / (1.0f - d));
}

/* Double-kink pulse phase distortion */
static inline f32 pd_pulse(f32 p, f32 warp)
{
    f32 w = clampf(0.5f - 0.44f * warp, 0.06f, 0.5f);
    if (p < w) return 0.5f * (p / w);
    if (p < 0.5f) return 0.5f;
    if (p < 0.5f + w) return 0.5f + 0.5f * ((p - 0.5f) / w);
    return 1.0f;
}

static struct {
    int algo;
    f32 warp, fold, bias, tone;
    f32 cut, q;
} DG;

static void digital_block(const Blk *b)
{
    DG.algo = b->algo & 7;
    DG.warp = b->k[0];
    DG.fold = b->k[1];
    DG.bias = b->k[2];
    DG.tone = b->k[3];
    DG.cut  = 240.0f * exp2fx(DG.tone * 6.3f);        /* 240 Hz .. 18.8 kHz */
    DG.q    = 0.72f + 1.85f * (1.0f - DG.tone) * (0.25f + 0.75f * DG.warp);
}

static void digital_note(Voice *v, const Blk *b)
{
    int i;
    (void)b;
    if (v->env < 0.001f) {
        for (i = 0; i < 6; i++) v->ph[i] = 0.0f;
        for (i = 0; i < 16; i++) v->s[i] = 0.0f;
        v->s[6] = 1.0f;
    }
    v->s[7] = 1.0f;  /* transient punch */
}

static void digital_render(Voice *v, const Blk *b, f32 *out_l, f32 *out_r, int n)
{
    int algo = DG.algo, i;
    f32 pitch = v->pitch + b->bend;
    f32 vel = lerpf(1.0f, 0.32f + 0.88f * v->vel_f, b->vel_sens);
    f32 menv = lerpf(1.0f, 0.18f + 1.25f * v->menv, b->menv_amt);
    f32 ks = clampf(exp2fx((62.0f - pitch) * (1.0f / 28.0f)), 0.30f, 1.45f);
    f32 warp = sat(DG.warp * lerpf(1.0f, 0.35f + 0.95f * v->menv, b->menv_amt * 0.65f));
    f32 fold_g = 1.0f + DG.fold * DG.fold * 4.4f * vel * menv * ks;
    f32 b0 = DG.bias * 0.65f;
    f32 dc0 = fold_sin(b0);
    f32 dc1 = fold_sin(b0 * 0.5f);
    f32 det = 1.0f + 0.0020f + 0.0065f * DG.bias;
    f32 f, d0, d1, dm, fc;
    f32 p0 = v->ph[0], p1 = v->ph[1], pm = v->ph[2];
    f32 plk = (algo == 3) ? exp2fx(-v->age * 2.2f) : 1.0f;
    Svf fl;

    if (b->punch > 0.01f) pitch += b->punch * v->s[7] * 0.85f;
    f = mtof(pitch);
    /* 2x oversampled increments */
    d0 = minf(f * b->isr * 0.5f, 0.42f);
    d1 = minf(d0 * det, 0.42f);
    dm = minf(d0 * (algo == 4 ? 2.76f : (algo == 2 ? 1.5f : 2.0f)), 0.42f);

    fc = clampf(DG.cut * exp2fx(v->menv * b->menv_amt * 2.8f + (v->vel_f - 0.5f) * b->vel_sens * 1.4f), 110.0f, 18500.0f);
    svf_set(&fl, fc, DG.q, b->isr);

    for (i = 0; i < n; i++) {
        f32 sl = 0.0f, sr = 0.0f;
        int os;
        for (os = 0; os < 2; os++) {
            f32 xl, xr;
            switch (algo) {
            case 0: /* CZ-SAW: phase-distorted saw into Buchla wavefolder */
                xl = -tcos(pd_warp(p0, warp));
                xr = -tcos(pd_warp(p1, warp));
                xl = fold_sin(xl * fold_g + b0) - dc0;
                xr = fold_sin(xr * fold_g + b0) - dc0;
                break;
            case 1: /* CZ-RES: Casio CZ windowed resonant harmonic peak */
            {
                f32 rm = 1.0f + (1.0f + 10.0f * warp) * (0.35f + 0.85f * menv) * ks;
                xl = tsin(p0 * rm) * (1.0f - p0) * 1.35f;
                xr = tsin(p1 * rm) * (1.0f - p1) * 1.35f;
                xl = fold_sin(xl * (1.0f + 0.55f * (fold_g - 1.0f)) + b0 * 0.5f) - dc1;
                xr = fold_sin(xr * (1.0f + 0.55f * (fold_g - 1.0f)) + b0 * 0.5f) - dc1;
                break;
            }
            case 2: /* BUCHLA: West-Coast complex osc (FM + 2-stage wavefolder) */
            {
                f32 mod = tsin(pm) * warp * 0.55f * menv * ks;
                f32 s1l = fold_sin(tsin(p0 + mod) * fold_g + b0) - dc0;
                f32 s1r = fold_sin(tsin(p1 + mod * 0.94f) * fold_g + b0) - dc0;
                f32 g2 = 1.0f + 1.35f * DG.fold;
                xl = fold_sin(s1l * g2 + b0 * 0.5f) - dc1;
                xr = fold_sin(s1r * g2 + b0 * 0.5f) - dc1;
                break;
            }
            case 3: /* PLUCK: crisp hyper-digital PD+fold mallet pluck */
            {
                f32 fg = 1.0f + (fold_g - 1.0f) * plk;
                xl = -tcos(pd_pulse(p0, warp * plk));
                xr = -tcos(pd_pulse(p1, warp * plk));
                xl = (fold_sin(xl * fg + b0) - dc0) * (0.45f + 0.55f * plk);
                xr = (fold_sin(xr * fg + b0) - dc0) * (0.45f + 0.55f * plk);
                break;
            }
            case 4: /* METRIC: metallic inharmonic FM + asymmetric wavefold */
            {
                f32 mod = tsin(pm) * (0.25f + 0.85f * warp) * menv * ks;
                xl = fold_sin(tsin(p0 + mod) * fold_g + b0) - dc0;
                xr = fold_sin(tsin(p1 - mod * 0.92f) * fold_g + b0) - dc0;
                break;
            }
            case 5: /* CYBER: octave-split folded PD lead + solid sub fundamental */
            {
                f32 p02 = fractf(p0 * 2.0f), p12 = fractf(p1 * 2.0f);
                f32 hi_l = fold_sin((-tcos(pd_warp(p02, warp))) * fold_g + b0) - dc0;
                f32 hi_r = fold_sin((-tcos(pd_warp(p12, warp))) * fold_g + b0) - dc0;
                xl = 0.52f * tsin(p0) + 0.58f * hi_l;
                xr = 0.52f * tsin(p1) + 0.58f * hi_r;
                break;
            }
            case 6: /* GLASSY: delicate double-pulse PD chime + soft 2nd-harmonic shimmer */
                xl = -tcos(pd_pulse(p0, warp * 0.8f)) * 0.65f + 0.40f * tsin(pm + tsin(p0) * warp * 0.6f);
                xr = -tcos(pd_pulse(p1, warp * 0.8f)) * 0.65f + 0.40f * tsin(pm + tsin(p1) * warp * 0.6f);
                xl = fold_sin(xl * (1.0f + 0.45f * (fold_g - 1.0f)) + b0 * 0.5f) - dc1;
                xr = fold_sin(xr * (1.0f + 0.45f * (fold_g - 1.0f)) + b0 * 0.5f) - dc1;
                break;
            default: /* CRUSH: stepped digital phase-quantized wavefolder */
            {
                f32 steps = 3.0f + 28.0f * (1.0f - warp * 0.88f);
                f32 q0 = (f32)(int)(p0 * steps) / steps;
                f32 q1 = (f32)(int)(p1 * steps) / steps;
                xl = fold_sin(tsin(q0) * fold_g + b0) - dc0;
                xr = fold_sin(tsin(q1) * fold_g + b0) - dc0;
                break;
            }
            }
            sl += xl * 0.5f;
            sr += xr * 0.5f;

            p0 += d0; if (p0 >= 1.0f) p0 -= 1.0f;
            p1 += d1; if (p1 >= 1.0f) p1 -= 1.0f;
            pm += dm; if (pm >= 1.0f) pm -= 1.0f;
        }
        {
            f32 lp, bp;
            SVF_TICK(fl, &v->s[8],  sl, lp, bp); (void)bp; sl = lp;
            SVF_TICK(fl, &v->s[10], sr, lp, bp); (void)bp; sr = lp;
        }
        out_l[i] = sl;
        out_r[i] = sr;
    }

    v->ph[0] = p0; v->ph[1] = p1; v->ph[2] = pm;
    v->s[7] *= 1.0f - onepole_k(0.020f, b->sr / (f32)n);
}

/* ---- 4-Color Vector Picture: Sculpted Transfer Monitor ------------------ */
static f32 dg_ph, dg_lev, dg_kick, dg_kickv;
static uint32_t dg_seen;

static void digital_draw(const Vis *vis)
{
    f32 dt = vis->dt, warp = vis->k[0], fold = vis->k[1], bias = vis->k[2], tone = vis->k[3];
    f32 xy[2 * 72], xy2[2 * 72];
    f32 x0 = 26.0f, x1 = 214.0f, cy = 114.0f, amp = 48.0f;
    f32 fold_g = 1.0f + fold * fold * 3.6f;
    f32 b0 = bias * 0.65f, dc0 = fold_sin(b0);
    int i;

    if (vis->fresh) {
        dg_seen = vis->notes;
        dg_lev = dg_kick = dg_kickv = 0.0f;
    }
    if (vis->notes != dg_seen) {
        dg_seen = vis->notes;
        dg_kickv += 5.0f;
    }
    spring_z(&dg_kick, &dg_kickv, 0.0f, 24.0f, 0.34f, dt);
    dg_lev += (minf(1.0f, vis->level * 3.6f) - dg_lev) * minf(1.0f, dt * 16.0f);
    dg_ph = fractf(dg_ph + dt * (0.20f + 0.55f * dg_lev));

    /* Retro oscilloscope frame & grid (Slate) */
    g_color(GC_SLATE);
    g_box(20.0f, 44.0f, 200.0f, 140.0f, 1.4f, 0.55f);
    g_line(22.0f, cy, 218.0f, cy, 1.0f, 0.25f);
    g_line(120.0f, 46.0f, 120.0f, 182.0f, 1.0f, 0.25f);

    /* Orange fold ceiling/floor rails (move inward as FOLD increases) */
    {
        f32 rail = 14.0f + 42.0f * (1.0f - fold * 0.72f);
        g_color(GC_ORANGE);
        g_line(24.0f, cy - rail, 216.0f, cy - rail, 1.3f, 0.45f + 0.35f * tone);
        g_line(24.0f, cy + rail, 216.0f, cy + rail, 1.3f, 0.45f + 0.35f * tone);
        /* Tone cutoff bar at bottom of monitor */
        g_line(26.0f, 176.0f, 26.0f + 188.0f * tone, 176.0f, 2.4f, 0.80f);
    }

    /* Sculpted mathematical PD waveform (Blue) & Folded waveform (Green) */
    for (i = 0; i < 72; i++) {
        f32 p = (f32)i * (1.0f / 71.0f);
        f32 px = lerpf(x0, x1, p);
        f32 pd = -tcos(pd_warp(fractf(p + dg_ph * 0.15f), warp));
        f32 fl = fold_sin(pd * (fold_g + 0.6f * dg_lev) + b0) - dc0;
        f32 sc = vis->scope[(vis->scope_pos + i * 3) & (SCOPE_N - 1)] * 20.0f;
        xy[2 * i]      = px;
        xy[2 * i + 1]  = clampf(cy - pd * (amp * 0.65f), 48.0f, 178.0f);
        xy2[2 * i]     = px;
        xy2[2 * i + 1] = clampf(cy - fl * (amp * (0.78f + 0.22f * dg_lev + 0.08f * dg_kick)) - sc * 0.55f, 48.0f, 178.0f);
    }
    g_color(GC_BLUE);
    g_poly(xy, 72, 0, 1.5f, 0.52f);
    g_color(GC_GREEN);
    g_poly(xy2, 72, 0, 2.5f, 0.95f);

    /* Phase distortion kink marker (White = K3 BIAS / K1 WARP node) */
    {
        f32 kx = lerpf(x0, x1, clampf(0.5f - 0.44f * warp, 0.06f, 0.5f));
        f32 ky = cy - bias * 28.0f;
        g_color(GC_WHITE);
        g_line(kx, 48.0f, kx, 178.0f, 1.1f, 0.35f);
        g_ring(kx, ky, 6.5f + 3.0f * dg_lev, 2.0f, 0.92f);
        g_disc(kx, ky, 2.6f, 0.95f);
    }
    g_color(GC_WHITE);
}

const InstDef inst_shaper = {
    "SHAPER",
    {42, 230, 224},
    {"WARP", "FOLD", "BIAS", "TONE"},
    {"CZ-SAW", "CZ-RES", "BUCHLA", "PLUCK", "METRIC", "CYBER", "GLASSY", "CRUSH"},
    {
        {48, 45, 22, 74},            /* K1..K4 defaults */
        0,                           /* algo: CZ-SAW */
        {0, 38, 62, 30},             /* Amp ADSR */
        {72, 34, 72, 24},            /* Mod/Filter Env */
        {0, 40, 0, 1},               /* LFO */
        {0, 3, 1, 60},               /* Arp */
        {0, 50, 34}                  /* Mix */
    },
    0.27f,
    digital_block, digital_note, digital_render, 0, digital_draw, 0
};
