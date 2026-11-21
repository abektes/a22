/* A22 - FM-4: Flagship 4-Operator Stereo FM Synthesizer + Post-FM Warm ZDF Filter.
 *
 * True 4-operator phase modulation with 8 classic topologies, 2x oversampled inner FM core
 * (silky alias-free high harmonics), DX7-style 2-sample averaged self-feedback, velocity &
 * Mod-Env scaled modulation index, stereo carrier detune, and per-voice post-FM resonant filter.
 *
 *   K1 (BLUE)   RATIO   harmonic / inharmonic modulator ratios (smooth crossfade)
 *   K2 (GREEN)  DEPTH   FM modulation index (scaled by Mod-Env, Velocity & Key-Scaling)
 *   K3 (WHITE)  FEEDBK  Op4 self-feedback + stereo carrier detune spread
 *   K4 (ORANGE) FILTER  per-voice post-FM warm resonant ZDF low-pass filter */
#include "internal.h"
#include "dsp.h"

typedef struct FmAlgo {
    uint8_t topo;        /* 0:2+2, 1:4-stack, 2:Y-branch, 3:3-car, 4:brass 2+2, 5:3->1, 6:4-par, 7:fb-ring */
    f32 crat;            /* base carrier ratio */
    f32 r2[4];           /* Op2 ratios walked by K1 */
    f32 r3[4];           /* Op3 ratios */
    f32 r4[4];           /* Op4 ratios */
    f32 idx;             /* base mod index scale */
    f32 det;             /* stereo carrier detune (cents-ish ratio) */
    f32 matt;            /* mod attack time (s), 0 = instant */
    f32 adec;            /* natural per-note decay rate (0 = sustain) */
} FmAlgo;

static const FmAlgo FA[NALGO] = {
    /* 0 KEYS  */ {0, 1.0f, {1.0f, 2.0f, 3.0f, 7.0f},  {1.0f, 1.0f, 2.0f, 1.0f},   {14.0f, 7.0f, 11.0f, 14.0f}, 0.58f, 0.0028f, 0.0f,  0.42f},
    /* 1 BASS  */ {1, 0.5f, {0.5f, 1.0f, 1.5f, 2.0f},  {1.0f, 2.0f, 3.0f, 4.0f},   {2.0f, 1.0f, 3.0f, 6.0f},    0.85f, 0.0012f, 0.0f,  0.15f},
    /* 2 BELL  */ {2, 1.0f, {1.414f, 2.0f, 2.76f, 3.5f},{2.76f, 3.81f, 5.4f, 7.1f},{5.4f, 7.1f, 9.2f, 11.3f},   0.62f, 0.0040f, 0.0f,  0.55f},
    /* 3 PAD   */ {3, 1.0f, {1.0f, 2.0f, 3.0f, 4.0f},  {0.5f, 1.0f, 1.5f, 2.0f},   {1.0f, 2.0f, 4.0f, 6.0f},    0.45f, 0.0065f, 0.14f, 0.0f},
    /* 4 BRASS */ {4, 1.0f, {1.0f, 1.0f, 2.0f, 3.0f},  {1.0f, 2.0f, 2.0f, 4.0f},   {1.0f, 3.0f, 4.0f, 5.0f},    0.72f, 0.0045f, 0.07f, 0.0f},
    /* 5 PLUCK */ {5, 1.0f, {2.0f, 3.0f, 4.0f, 5.0f},  {3.0f, 5.0f, 7.0f, 9.0f},   {1.0f, 2.0f, 3.5f, 6.0f},    0.66f, 0.0022f, 0.0f,  1.25f},
    /* 6 ORGAN */ {6, 1.0f, {2.0f, 2.0f, 3.0f, 4.0f},  {3.0f, 4.0f, 6.0f, 8.0f},   {4.0f, 6.0f, 8.0f, 12.0f},   0.38f, 0.0038f, 0.0f,  0.0f},
    /* 7 ALIEN */ {7, 1.0f, {1.41f, 2.33f, 3.14f, 5.1f},{2.71f, 3.77f, 6.13f, 8.3f},{0.5f, 1.73f, 4.19f, 9.7f}, 0.95f, 0.0075f, 0.0f,  0.0f},
};

static struct {
    int algo;
    f32 r2, r3, r4;
    f32 depth, fb, det, cut, q;
} F;

static void fm4_block(const Blk *b)
{
    const FmAlgo *a = &FA[b->algo & 7];
    f32 x = sat(b->k[0]) * 3.0f;
    int i0 = iclamp((int)x, 0, 2);
    f32 fr = x - (f32)i0;
    /* smooth musical snap near integer ratios while allowing continuous sweeps */
    fr = smooth01(fr);
    F.algo = b->algo & 7;
    F.r2 = lerpf(a->r2[i0], a->r2[i0 + 1], fr);
    F.r3 = lerpf(a->r3[i0], a->r3[i0 + 1], fr);
    F.r4 = lerpf(a->r4[i0], a->r4[i0 + 1], fr);
    F.depth = (0.04f + b->k[1] * sqrtfx(b->k[1]) * 1.15f) * a->idx;
    F.fb = b->k[2] * b->k[2] * 0.42f;
    F.det = a->det * (0.4f + 1.8f * b->k[2]);
    F.cut = 220.0f * exp2fx(b->k[3] * 6.4f);   /* 220 Hz .. 18.6 kHz */
    F.q = 0.72f + 1.4f * (1.0f - b->k[3]) * b->k[1];
}

static void fm4_note(Voice *v, const Blk *b)
{
    int i;
    (void)b;
    if (v->env < 0.001f) {
        for (i = 0; i < 6; i++) v->ph[i] = 0.0f;
        for (i = 0; i < 12; i++) v->s[i] = 0.0f;
        v->s[6] = 1.0f;  /* amp body */
        v->s[7] = 1.0f;  /* punch pitch env */
    } else {
        v->s[7] = 1.0f;
    }
}

static void fm4_render(Voice *v, const Blk *b, f32 *out_l, f32 *out_r, int n)
{
    const FmAlgo *a = &FA[F.algo];
    f32 pitch = v->pitch + b->bend;
    f32 punch = b->punch * v->s[7];
    f32 ks = clampf(exp2fx((60.0f - pitch) * (1.0f / 26.0f)), 0.28f, 1.55f);
    f32 vel_mod = lerpf(1.0f, 0.35f + 0.85f * v->vel_f, b->vel_sens);
    f32 menv = lerpf(1.0f, 0.15f + 1.35f * v->menv, b->menv_amt);
    f32 matt = a->matt > 0.0f ? (0.22f + 0.78f * sat(v->age / a->matt)) : 1.0f;
    f32 idx_tgt = F.depth * ks * vel_mod * menv * matt;
    f32 idx = v->s[0], didx = (idx_tgt - idx) / (f32)n;
    f32 f, dc1, dc2, d2, d3, d4, fc;
    f32 p1 = v->ph[0], p1r = v->ph[1], p2 = v->ph[2], p3 = v->ph[3], p4 = v->ph[4];
    f32 fb0 = v->s[1], fb1 = v->s[2], amp = v->s[6], amp1, damp;
    f32 fb = F.fb;
    Svf fl;
    int i, topo = a->topo;

    if (punch > 0.001f)
        pitch += punch * 0.45f * (topo == 1 ? 3.5f : 1.0f);
    f = mtof(pitch);
    /* 2x oversampled increments (half step per sub-sample) */
    dc1 = minf(f * a->crat * b->isr * 0.5f, 0.42f);
    dc2 = minf(dc1 * (1.0f + F.det), 0.42f);
    d2 = minf(f * F.r2 * b->isr * 0.5f, 0.42f);
    d3 = minf(f * F.r3 * b->isr * 0.5f, 0.42f);
    d4 = minf(f * F.r4 * b->isr * 0.5f, 0.42f);

    /* per-voice post-FM filter modulated by Mod-Env and Velocity */
    fc = clampf(F.cut * exp2fx((v->menv * b->menv_amt * 2.8f) + (v->vel_f - 0.5f) * b->vel_sens * 1.4f), 90.0f, 18500.0f);
    svf_set(&fl, fc, F.q, b->isr);

    amp1 = a->adec > 0.0f ? exp2fx(-v->age * a->adec * (0.65f + 0.35f / ks)) : 1.0f;
    damp = (amp1 - amp) / (f32)n;

    for (i = 0; i < n; i++) {
        f32 sl = 0.0f, sr = 0.0f;
        int os;
        /* 2x oversampled 4-op FM core */
        for (os = 0; os < 2; os++) {
            f32 o4 = tsin(p4 + fb * 0.5f * (fb0 + fb1));
            f32 o3, o2, ol, or_;
            fb1 = fb0;
            fb0 = o4;

            switch (topo) {
            case 0: /* KEYS: dual 2+2 parallel pairs with stereo carrier detune */
                o3 = tsin(p3 + o4 * idx * 0.45f * v->menv);
                o2 = tsin(p2 + o4 * idx * 0.12f);
                ol = 0.68f * tsin(p1 + o2 * idx) + 0.32f * o3;
                or_ = 0.68f * tsin(p1r + o2 * idx * 0.96f) + 0.32f * tsin(p3 + 0.08f + o4 * idx * 0.45f * v->menv);
                break;
            case 1: /* BASS: 4-stack 4->3->2->1 + warm sub fundamental */
                o3 = tsin(p3 + o4 * idx * 0.65f);
                o2 = tsin(p2 + o3 * idx * 0.85f);
                ol = 0.78f * tsin(p1 + o2 * idx) + 0.25f * tsin(p1);
                or_ = 0.78f * tsin(p1r + o2 * idx * 0.98f) + 0.25f * tsin(p1r);
                break;
            case 2: /* BELL: Y-branch (4+3)->2->1 with faster decay on upper chime Op4 */
                o3 = tsin(p3);
                o2 = tsin(p2 + (o4 * (0.24f + 0.44f * v->menv) + o3 * 0.54f) * idx);
                ol = tsin(p1 + o2 * idx * 0.85f);
                or_ = tsin(p1r + o2 * idx * 0.85f + 0.15f * o3 * idx);
                break;
            case 3: /* PAD: 4 -> 3 stereo detuned carriers */
                o3 = tsin(p3 + o4 * idx * 0.75f);
                o2 = tsin(p2 + o4 * idx * 0.55f);
                ol = 0.45f * tsin(p1 + o4 * idx) + 0.38f * o2 + 0.25f * o3;
                or_ = 0.45f * tsin(p1r + o4 * idx * 0.92f) + 0.25f * o2 + 0.38f * o3;
                break;
            case 4: /* BRASS: parallel rich horn pairs with swell bloom */
                o3 = tsin(p3 + o4 * idx * (0.45f + 0.45f * matt));
                o2 = tsin(p2 + o4 * idx * 0.42f);
                ol = 0.6f * tsin(p1 + o2 * idx) + 0.45f * o3;
                or_ = 0.6f * tsin(p1r + o3 * idx * 0.9f) + 0.45f * o2;
                break;
            case 5: /* PLUCK: 3 modulators -> 1 carrier with percussive upper partial decay */
                o3 = tsin(p3);
                o2 = tsin(p2);
                {
                    f32 pm_env = 0.25f + 0.75f * v->menv;
                    ol = tsin(p1 + (o2 * 0.52f + (o3 * 0.36f + o4 * 0.28f) * pm_env) * idx);
                    or_ = tsin(p1r + (o2 * 0.46f + (o3 * 0.40f + o4 * 0.28f) * pm_env) * idx);
                }
                break;
            case 6: /* ORGAN: 4-parallel harmonic additive + soft cross-FM */
                o2 = tsin(p2 + 0.18f * o4 * idx);
                o3 = tsin(p3 + 0.14f * o4 * idx);
                ol = 0.42f * tsin(p1 + 0.15f * o2 * idx) + 0.28f * o2 + 0.18f * o3 + 0.12f * o4;
                or_ = 0.42f * tsin(p1r + 0.15f * o3 * idx) + 0.24f * o2 + 0.22f * o3 + 0.12f * o4;
                break;
            default: /* ALIEN: cross-feedback + ring-FM */
                o3 = tsin(p3 + o4 * idx * 0.95f);
                o2 = tsin(p2 + o3 * idx * 0.85f);
                ol = tsin(p1 + o2 * idx) * (0.45f + 0.55f * o3);
                or_ = tsin(p1r + o3 * idx) * (0.45f + 0.55f * o2);
                break;
            }
            sl += ol * 0.5f;
            sr += or_ * 0.5f;

            p1 += dc1;  if (p1 >= 1.0f) p1 -= 1.0f;
            p1r += dc2; if (p1r >= 1.0f) p1r -= 1.0f;
            p2 += d2;   if (p2 >= 1.0f) p2 -= 1.0f;
            p3 += d3;   if (p3 >= 1.0f) p3 -= 1.0f;
            p4 += d4;   if (p4 >= 1.0f) p4 -= 1.0f;
        }
        /* Post-FM warm stereo ZDF filter */
        {
            f32 lp, bp;
            SVF_TICK(fl, &v->s[8],  sl, lp, bp); (void)bp; sl = lp;
            SVF_TICK(fl, &v->s[10], sr, lp, bp); (void)bp; sr = lp;
        }
        out_l[i] = sl * amp;
        out_r[i] = sr * amp;
        idx += didx;
        amp += damp;
    }

    v->ph[0] = p1; v->ph[1] = p1r; v->ph[2] = p2; v->ph[3] = p3; v->ph[4] = p4;
    v->s[0] = idx; v->s[1] = fb0; v->s[2] = fb1; v->s[6] = amp;
    v->s[7] *= 1.0f - onepole_k(0.025f, b->sr / (f32)n);
}

/* ---- 4-Color Vector Picture: 4-Op Topology Matrix + Live FM Waveform Ring ------- */
static f32 fm_rot, fm_kick, fm_kickv, fm_lev;
static uint32_t fm_seen;

/* operator node coordinates (4 nodes: Op4 Blue, Op3 Green, Op2 White, Op1 Orange) */
static const f32 NODE_XY[8][4][2] = {
    /* 0 KEYS 2+2   */ {{86, 76},  {154, 76},  {86, 152},  {154, 152}},
    /* 1 BASS stack */ {{120, 58}, {120, 95},  {120, 132}, {120, 169}},
    /* 2 BELL Y     */ {{84, 70},  {156, 70},  {120, 114}, {120, 164}},
    /* 3 PAD fan    */ {{120, 66}, {70, 144},  {120, 158}, {170, 144}},
    /* 4 BRASS      */ {{120, 64}, {82, 114},  {158, 114}, {120, 164}},
    /* 5 PLUCK 3->1 */ {{72, 78},  {120, 66},  {168, 78},  {120, 160}},
    /* 6 ORGAN par  */ {{58, 114}, {99, 114},  {141, 114}, {182, 114}},
    /* 7 ALIEN ring */ {{120, 66}, {168, 114}, {120, 162}, {72, 114}},
};
/* modulation edges per topology (up to 4 pairs of [src, dst], -1 end) */
static const int8_t EDGES[8][4][2] = {
    {{0, 2}, {1, 3}, {0, 1}, {-1, -1}},
    {{0, 1}, {1, 2}, {2, 3}, {-1, -1}},
    {{0, 2}, {1, 2}, {2, 3}, {-1, -1}},
    {{0, 1}, {0, 2}, {0, 3}, {-1, -1}},
    {{0, 1}, {0, 2}, {1, 3}, {2, 3}},
    {{0, 3}, {1, 3}, {2, 3}, {-1, -1}},
    {{0, 1}, {1, 2}, {2, 3}, {-1, -1}},
    {{0, 1}, {1, 2}, {2, 3}, {3, 0}},
};

/* carrier bitmask per topology (bit i = 1 if node i is an audible carrier) */
static const uint8_t CAR_MASK[8] = {
    0x0A, /* 0 KEYS: Op3, Op1 */
    0x08, /* 1 BASS: Op1 */
    0x08, /* 2 BELL: Op1 */
    0x0E, /* 3 PAD:  Op3, Op2, Op1 */
    0x0A, /* 4 BRASS:Op3, Op1 */
    0x08, /* 5 PLUCK:Op1 */
    0x0F, /* 6 ORGAN:all 4 parallel */
    0x0C, /* 7 ALIEN:Op2, Op1 ring */
};

static void fm4_draw(const Vis *vis)
{
    f32 dt = vis->dt, ratio = vis->k[0], depth = vis->k[1], fb = vis->k[2], flt = vis->k[3];
    int algo = vis->algo & 7, i;
    f32 xy[2 * 80], cx = 120.0f, cy = 114.0f;
    static const int NC[4] = {GC_BLUE, GC_GREEN, GC_WHITE, GC_ORANGE};

    if (vis->fresh) {
        fm_seen = vis->notes;
        fm_kick = fm_kickv = fm_lev = 0.0f;
    }
    if (vis->notes != fm_seen) {
        fm_seen = vis->notes;
        fm_kickv += 4.5f;
    }
    spring_z(&fm_kick, &fm_kickv, 0.0f, 22.0f, 0.35f, dt);
    fm_lev += (minf(1.0f, vis->level * 3.4f) - fm_lev) * minf(1.0f, dt * 16.0f);
    fm_rot = fractf(fm_rot + dt * (0.04f + 0.14f * depth + 0.12f * fm_lev));

    /* Subtle coordinate crosshairs in Slate */
    g_color(GC_SLATE);
    g_ring(cx, cy, 78.0f, 1.1f, 0.28f);
    g_line(cx - 86.0f, cy, cx + 86.0f, cy, 1.0f, 0.16f);
    g_line(cx, cy - 78.0f, cx, cy + 78.0f, 1.0f, 0.16f);

    /* Filter halo ring (Orange = K4 FILTER) */
    g_color(GC_ORANGE);
    g_arc(cx, cy, 78.0f, -0.25f, -0.25f + 0.99f * flt, 2.4f, 0.72f + 0.25f * fm_lev);

    /* Outer FM Phase-Modulated Waveform Ring (Green = K2 DEPTH, Blue = K1 RATIO) */
    {
        f32 lobes = 2.0f + (f32)(int)(ratio * 6.0f + 0.5f);
        f32 mod_a = 0.06f + 0.42f * depth + 0.18f * fm_lev;
        f32 r0 = 58.0f + 7.0f * flt + 4.0f * fm_kick;
        f32 amp = 5.0f + 13.0f * depth + 8.0f * fm_lev;
        for (i = 0; i < 80; i++) {
            f32 th = (f32)i * (1.0f / 80.0f);
            f32 m = sin_turns(lobes * th + fm_rot) + fb * 0.65f * sin_turns(lobes * 2.0f * th - fm_rot * 2.0f);
            f32 w = sin_turns((lobes * 0.5f + 1.0f) * th + mod_a * m);
            f32 r = r0 + amp * w;
            xy[2 * i]     = cx + cos_turns(th - 0.25f) * r;
            xy[2 * i + 1] = cy + sin_turns(th - 0.25f) * r;
        }
        g_color(GC_BLUE);
        g_poly(xy, 80, 1, 1.5f, 0.42f);
        g_color(GC_GREEN);
        for (i = 0; i < 80; i++) {
            f32 th = (f32)i * (1.0f / 80.0f);
            f32 s = vis->scope[(vis->scope_pos + i * 4) & (SCOPE_N - 1)];
            f32 r = r0 + s * (26.0f + 16.0f * depth);
            xy[2 * i]     = cx + cos_turns(th - 0.25f) * r;
            xy[2 * i + 1] = cy + sin_turns(th - 0.25f) * r;
        }
        g_poly(xy, 80, 1, 2.2f, 0.85f);
    }

    /* Modulation routing cables + traveling signal pulses between the 4 operator nodes */
    for (i = 0; i < 4; i++) {
        int a0 = EDGES[algo][i][0], a1 = EDGES[algo][i][1];
        f32 x0, y0, x1, y1, u;
        if (a0 < 0) continue;
        x0 = NODE_XY[algo][a0][0]; y0 = NODE_XY[algo][a0][1];
        x1 = NODE_XY[algo][a1][0]; y1 = NODE_XY[algo][a1][1];
        g_color(NC[a0]);
        g_line(x0, y0, x1, y1, 1.6f + 1.8f * depth, 0.55f + 0.4f * fm_lev);
        u = fractf(fm_rot * 3.0f + (f32)i * 0.25f);
        g_color(GC_GOLD);
        g_disc(lerpf(x0, x1, u), lerpf(y0, y1, u), 2.2f + 1.5f * fm_lev, 0.65f + 0.35f * fm_lev);
    }

    /* Feedback self-loop on Op4 (White = K3 FEEDBK) */
    if (fb > 0.02f) {
        g_color(GC_WHITE);
        g_ring(NODE_XY[algo][0][0] - 11.0f, NODE_XY[algo][0][1] - 11.0f,
               7.0f + 5.0f * fb, 1.6f, 0.5f + 0.5f * fb);
    }

    /* Draw the 4 Operator Nodes (carriers get a double outer ring, modulators get single ring) */
    for (i = 0; i < 4; i++) {
        f32 nx = NODE_XY[algo][i][0], ny = NODE_XY[algo][i][1];
        f32 nr = 10.5f + 4.0f * vis->k[i] + 3.0f * fm_lev + 2.0f * fm_kick;
        int is_car = (CAR_MASK[algo] >> i) & 1;
        g_mode(G_PAINT);
        g_disc(nx, ny, nr + (is_car ? 4.0f : 2.0f), 0.0f);
        g_mode(G_MAX);
        g_color(NC[i]);
        if (is_car)
            g_ring(nx, ny, nr + 3.2f, 1.2f, 0.52f + 0.35f * fm_lev);
        g_ring(nx, ny, nr, 2.2f, 0.95f);
        g_disc(nx, ny, 3.0f + 4.5f * vis->k[i] * (0.45f + 0.55f * fm_lev), 0.9f);
    }
    g_color(GC_WHITE);
}

const InstDef inst_fm4 = {
    "FM-4",
    {59, 158, 255},
    {"RATIO", "DEPTH", "FEEDBK", "FILTER"},
    {"KEYS", "BASS", "BELL", "PAD", "BRASS", "PLUCK", "ORGAN", "ALIEN"},
    {
        {25, 58, 20, 76},            /* K1..K4 defaults */
        0,                           /* algo: KEYS */
        {0, 42, 65, 34},             /* Amp ADSR */
        {65, 36, 70, 20},            /* Mod/Filter Env: depth, decay, vel, punch */
        {0, 45, 0, 1},               /* LFO */
        {0, 3, 1, 65},               /* Arp */
        {0, 50, 38}                  /* Mix: cut=0 (full), pan=50, fx_send=38 */
    },
    0.34f,
    fm4_block, fm4_note, fm4_render, 0, fm4_draw, 0
};
