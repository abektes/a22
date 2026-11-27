/* A22 - HAZE: Ambient Evolving Spectral & Formant FM.
 *
 * Lush cinematic atmosphere: 3 stereo FM/spectral oscillator pairs with organic pitch
 * wander, upper-octave crystalline shimmer, and Dual Parallel ZDF Formant Resonators
 * (sweeping smoothly across vowel/spectral formants OO -> OH -> AH -> EH -> EE) blended
 * with a warm evolving FM core.
 *
 *   K1 (BLUE)   MORPH    timbral morph (hollow pure core -> rich evolving FM partials)
 *   K2 (GREEN)  FORMANT  dual parallel resonant formant / spectral peak sweep
 *   K3 (WHITE)  DRIFT    organic tape wow/flutter & stereo phase drift movement
 *   K4 (ORANGE) SHIMMER  upper-octave crystalline FM shimmer & airy openness */
#include "internal.h"
#include "dsp.h"

/* Dual-formant vowel/spectral table (OO, OH, AH, EH, EE) */
static const f32 DM_F1[5] = {280.0f, 460.0f, 710.0f, 520.0f, 290.0f};
static const f32 DM_F2[5] = {820.0f, 940.0f, 1240.0f, 1860.0f, 2320.0f};

typedef struct DimAlgo {
    f32 r_mod;      /* primary FM modulator ratio */
    f32 r_sub;      /* secondary harmonic/sub partial ratio */
    f32 r_shim;     /* upper shimmer partial ratio */
    f32 fmix;       /* base formant resonator wet blend */
    f32 fm_base;    /* base FM index */
    f32 det;        /* stereo pair detune */
    int8_t oct;     /* octave offset */
} DimAlgo;

static const DimAlgo DMA[NALGO] = {
    /* 0 AETHER */ {2.00f,  1.50f, 2.00f, 0.34f, 0.55f, 0.0055f,  0},
    /* 1 VOX    */ {1.00f,  2.00f, 2.00f, 0.78f, 0.65f, 0.0042f,  0},
    /* 2 SOLAR  */ {1.50f,  0.50f, 3.00f, 0.30f, 0.70f, 0.0068f,  0},
    /* 3 ICE    */ {1.414f, 2.76f, 4.00f, 0.25f, 0.80f, 0.0050f,  0},
    /* 4 MIST   */ {1.00f,  0.50f, 2.00f, 0.42f, 0.38f, 0.0075f,  0},
    /* 5 ABYSS  */ {0.50f,  1.50f, 1.50f, 0.58f, 0.75f, 0.0060f, -1},
    /* 6 PRISM  */ {3.00f,  1.50f, 3.00f, 0.28f, 0.62f, 0.0058f,  0},
    /* 7 GHOST  */ {2.01f,  1.49f, 2.99f, 0.68f, 0.58f, 0.0095f,  0},
};

static struct {
    int algo;
    f32 morph, form, drift, shim;
    f32 lfo1, lfo2;
    Svf air;        /* the airy smoothing low-pass: SHIMMER's alone, so once a block, not once a voice */
} DM;
static f32 dm_p1, dm_p2, dm_t_was;

static void haze_block(const Blk *b)
{
    f32 dt = b->step;
    if (b->time < dm_t_was) dm_p1 = dm_p2 = 0.0f;
    dm_t_was = b->time;
    dm_p1 = fractf(dm_p1 + (0.23f + 0.62f * b->k[2]) * dt);
    dm_p2 = fractf(dm_p2 + (0.47f + 0.95f * b->k[2]) * dt);

    DM.algo  = b->algo & 7;
    DM.morph = b->k[0];
    DM.form  = b->k[1];
    DM.drift = b->k[2];
    DM.shim  = b->k[3];
    DM.lfo1  = tsin(dm_p1);
    DM.lfo2  = tsin(dm_p2);
    svf_set(&DM.air, clampf(480.0f * exp2fx((0.35f + 0.65f * DM.shim) * 5.2f), 320.0f, 17500.0f), 0.75f, b->isr);
}

static void haze_note(Voice *v, const Blk *b)
{
    int i;
    (void)b;
    if (v->env < 0.001f) {
        for (i = 0; i < 8; i++) v->ph[i] = fractf(v->r1 * (f32)(i + 1) * 0.23f + v->r2 * 0.17f);
        for (i = 0; i < 16; i++) v->s[i] = 0.0f;
    }
}

H1_HOT static void haze_render(Voice *v, const Blk *b, f32 *out_l, f32 *out_r, int n)
{
    const DimAlgo *a = &DMA[DM.algo];
    f32 pitch = v->pitch + (f32)(a->oct * 12) + b->bend;
    f32 vel = lerpf(1.0f, 0.35f + 0.85f * v->vel_f, b->vel_sens);
    f32 menv = lerpf(1.0f, 0.25f + 1.15f * v->menv, b->menv_amt);
    f32 ks = clampf(exp2fx((60.0f - pitch) * (1.0f / 26.0f)), 0.28f, 1.45f);
    /* Slow evolving swell of FM index & shimmer over note age */
    f32 bloom = 0.42f + 0.58f * sat(v->age * 1.4f);
    f32 wander = (0.04f + 0.36f * DM.drift) * (0.55f * tsin(v->ph[7]) + 0.45f * DM.lfo1);
    f32 f = mtof(pitch + wander);
    f32 det = a->det * (0.45f + 2.1f * DM.drift);
    f32 d0 = minf(f * b->isr, 0.43f);
    f32 d1 = minf(d0 * (1.0f + det), 0.43f);
    f32 dm = minf(f * a->r_mod * b->isr, 0.43f);
    f32 ds = minf(f * a->r_sub * b->isr, 0.43f);
    f32 dsh = minf(f * a->r_shim * (1.0f + det * 0.6f) * b->isr, 0.43f);
    f32 dw = (0.14f + 0.35f * v->r2 + 0.45f * DM.drift) * b->isr;
    f32 idx = a->fm_base * (0.12f + 1.45f * DM.morph) * vel * menv * ks * bloom;
    f32 sh_amt = DM.shim * (0.35f + 0.65f * bloom);
    f32 fmix = clampf(a->fmix * (0.35f + 0.85f * DM.form), 0.0f, 0.85f);
    f32 p0 = v->ph[0], p1 = v->ph[1], pm = v->ph[2], ps = v->ph[3], psh = v->ph[4], pw = v->ph[7];
    Svf fm1, fm2, air = DM.air;
    int i;

    /* Interpolate dual formant frequencies smoothly across 5 vowels + slow drift + Mod-Env */
    {
        f32 vx = clampf(DM.form * 4.0f + DM.lfo2 * DM.drift * 0.45f + (v->menv - 0.5f) * b->menv_amt * 0.7f, 0.0f, 3.99f);
        int vi = (int)vx;
        f32 vf = vx - (f32)vi;
        f32 f1 = lerpf(DM_F1[vi], DM_F1[vi + 1], vf);
        f32 f2 = lerpf(DM_F2[vi], DM_F2[vi + 1], vf);
        f32 q1 = 2.6f + 2.8f * DM.morph;
        f32 q2 = 3.2f + 3.2f * DM.morph;
        svf_set(&fm1, f1, q1, b->isr);
        svf_set(&fm2, f2, q2, b->isr);
    }

    for (i = 0; i < n; i++) {
        f32 m0 = tsin(pm);
        f32 m1 = tsin(ps + m0 * idx * 0.45f);
        f32 c_l = tsin(p0 + (m0 * 0.68f + m1 * 0.32f) * idx);
        f32 c_r = tsin(p1 + (m1 * 0.68f + m0 * 0.32f) * idx);
        /* Add subtle spectral saw/tri harmonic body as MORPH rises so formants have rich content to sculpt */
        f32 body_l = lerpf(c_l, 0.62f * c_l + 0.38f * saw_bl(p0, d0), DM.morph * 0.75f);
        f32 body_r = lerpf(c_r, 0.62f * c_r + 0.38f * saw_bl(p1, d1), DM.morph * 0.75f);
        /* Crystalline upper shimmer partial */
        f32 shim_l = tsin(psh + m0 * idx * 0.5f) * sh_amt * 0.48f;
        f32 shim_r = tsin(psh + 0.25f - m1 * idx * 0.5f) * sh_amt * 0.48f;

        f32 lp1, lp2, b1l, b2l, b1r, b2r, hpl, hpr, xl, xr;
        SVF_TICK(fm1, &v->s[0], body_l, lp1, b1l); (void)lp1;
        SVF_TICK(fm2, &v->s[2], body_l, lp2, b2l);
        hpl = body_l - lp2 - b2l * fm2.k;
        SVF_TICK(fm1, &v->s[4], body_r, lp1, b1r); (void)lp1;
        SVF_TICK(fm2, &v->s[6], body_r, lp2, b2r);
        hpr = body_r - lp2 - b2r * fm2.k;

        xl = lerpf(body_l, b1l * 0.95f + b2l * 0.82f + hpl * 0.14f * sh_amt, fmix) + shim_l;
        xr = lerpf(body_r, b1r * 0.95f + b2r * 0.82f + hpr * 0.14f * sh_amt, fmix) + shim_r;

        /* Warm airy stereo smoothing LPF */
        SVF_TICK(air, &v->s[8],  xl, lp1, b1l); (void)b1l; xl = lp1;
        SVF_TICK(air, &v->s[10], xr, lp2, b1r); (void)b1r; xr = lp2;

        out_l[i] = xl;
        out_r[i] = xr;

        p0  += d0;  if (p0 >= 1.0f)  p0 -= 1.0f;
        p1  += d1;  if (p1 >= 1.0f)  p1 -= 1.0f;
        pm  += dm;  if (pm >= 1.0f)  pm -= 1.0f;
        ps  += ds;  if (ps >= 1.0f)  ps -= 1.0f;
        psh += dsh; if (psh >= 1.0f) psh -= 1.0f;
        pw  += dw;  if (pw >= 1.0f)  pw -= 1.0f;
    }

    v->ph[0] = p0; v->ph[1] = p1; v->ph[2] = pm; v->ph[3] = ps; v->ph[4] = psh; v->ph[7] = pw;
}

/* ---- 4-Color Vector Picture: 3D Spectral Formant Terrain + Morphing Portal ------- */
static f32 dm_z, dm_rot, dm_lev;

static void haze_draw(const Vis *vis)
{
    f32 dt = vis->dt, morph = vis->k[0], form = vis->k[1], drift = vis->k[2], shim = vis->k[3];
    f32 cx = 120.0f, cy = 84.0f;
    static const int LC[5] = {GC_BLUE, GC_CYAN, GC_GREEN, GC_WHITE, GC_ORANGE};
    int algo = vis->algo & 7, k, i;

    if (vis->fresh) dm_lev = 0.0f;
    dm_lev += (minf(1.0f, vis->level * 3.8f) - dm_lev) * minf(1.0f, dt * 14.0f);
    dm_z   = fractf(dm_z + dt * (0.14f + 0.30f * drift + 0.18f * dm_lev));
    dm_rot = fractf(dm_rot + dt * (0.025f + 0.09f * drift));

    /* Soft spectral aura glow in upper sky */
    g_color(GC_CYAN);
    g_glow(cx, cy, 32.0f + 26.0f * shim, 0.18f + 0.24f * shim);

    /* Upper Sky: 4 nested morphing portal rings (diamond -> octagon -> star) */
    for (k = 0; k < 4; k++) {
        f32 depth = fractf(dm_z + (f32)k * 0.25f);
        f32 rad = 10.0f + depth * depth * 42.0f;
        f32 sq = 0.55f + 0.35f * form;
        f32 ang0 = dm_rot + depth * (0.18f * morph) + 0.125f + (f32)algo * 0.04f;
        f32 xy[2 * 8];
        f32 alpha = sin_turns(depth * 0.5f) * (0.50f + 0.45f * dm_lev);
        for (i = 0; i < 8; i++) {
            f32 th = ang0 + (f32)i * (1.0f / 8.0f);
            f32 star = (i & 1) ? (1.0f - 0.38f * morph) : (1.0f + 0.14f * morph);
            f32 r = rad * star;
            xy[2 * i]     = cx + cos_turns(th) * r;
            xy[2 * i + 1] = cy + sin_turns(th) * r * sq;
        }
        g_color(LC[k]);
        g_poly(xy, 8, 1, 1.2f + 1.1f * depth, alpha);
    }

    /* Lower Horizon: 5 3D Isometric Stacked Spectral/Formant Ridges (back to front) */
    for (k = 0; k < 5; k++) {
        f32 z = (f32)k * 0.25f;                         /* 0 = back horizon, 1 = front ridge */
        f32 y_base = lerpf(134.0f, 184.0f, z);
        f32 x_span = lerpf(64.0f, 96.0f, z);
        f32 p1 = 0.24f + 0.22f * form + 0.04f * sin_turns(dm_rot + z * 0.5f);
        f32 p2 = 0.54f + 0.30f * form - 0.04f * cos_turns(dm_rot * 1.3f + z);
        f32 h_scale = (14.0f + 18.0f * morph + 12.0f * dm_lev) * (0.55f + 0.45f * z);
        f32 rxy[2 * 42];
        rxy[0] = cx - x_span;
        rxy[1] = y_base;
        for (i = 0; i < 40; i++) {
            f32 u = (f32)i * (1.0f / 39.0f);
            f32 d1 = (u - p1) * (7.0f + 4.0f * morph);
            f32 d2 = (u - p2) * (6.5f + 4.0f * morph);
            f32 f1_pk = 1.0f / (1.0f + d1 * d1);
            f32 f2_pk = (0.75f + 0.25f * shim) / (1.0f + d2 * d2);
            f32 rip = 0.18f * morph * sin_turns(u * (3.0f + (f32)algo) + dm_z * 2.0f + z);
            f32 env = sin_turns(u * 0.5f);
            f32 h = (f1_pk + f2_pk + rip) * env * h_scale;
            rxy[2 * (i + 1)]     = cx + (u * 2.0f - 1.0f) * x_span;
            rxy[2 * (i + 1) + 1] = clampf(y_base - h, 46.0f, 192.0f);
        }
        rxy[2 * 41]     = cx + x_span;
        rxy[2 * 41 + 1] = y_base;

        if (k == 4) {
            g_color(GC_CYAN);
            g_fillv(rxy, 42, y_base - 36.0f, 0.30f + 0.16f * dm_lev, y_base, 0.03f);
        }
        g_color(LC[k]);
        g_poly(rxy + 2, 40, 0, 1.2f + 1.2f * z, 0.35f + 0.60f * z);
    }

    /* Shimmer harmonic star particles (Orange/Gold = K4 SHIMMER) */
    {
        int nstar = 4 + (int)(shim * 8.0f);
        for (i = 0; i < nstar; i++) {
            f32 th = (f32)i * 0.161803f + dm_rot * (1.0f + 0.3f * (f32)(i & 3));
            f32 r = 18.0f + 54.0f * fractf((f32)i * 0.37f + dm_z * 0.6f);
            f32 sx = cx + cos_turns(th) * r * 1.15f;
            f32 sy = cy + sin_turns(th) * r * 0.58f;
            f32 tw = 0.45f + 0.55f * sin_turns(vis->t * 1.4f + (f32)i * 0.21f);
            g_color((i & 1) ? GC_ORANGE : GC_GOLD);
            g_disc(sx, sy, 1.6f + 2.6f * shim * tw + 2.0f * dm_lev, 0.50f + 0.50f * tw);
        }
    }
    g_color(GC_WHITE);
}

const InstDef inst_haze = {
    "HAZE",
    {155, 118, 255},
    {"MORPH", "FORMANT", "DRIFT", "SHIMMER"},
    {"AETHER", "VOX", "SOLAR", "ICE", "MIST", "ABYSS", "PRISM", "GHOST"},
    {
        {46, 48, 38, 56},            /* K1..K4 defaults */
        0,                           /* algo: AETHER */
        {26, 56, 86, 60},            /* lush ambient Amp ADSR */
        {50, 58, 55, 0},             /* Mod/Filter Env */
        {0, 22, 0, 1},               /* LFO */
        {0, 2, 2, 80},               /* Arp */
        {0, 50, 54}                  /* Mix: deep spacious FX send */
    },
    0.22f,
    haze_block, haze_note, haze_render, 0, haze_draw, 0
};
