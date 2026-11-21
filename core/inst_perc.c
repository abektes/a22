/* A22 - PERC: Modal & Tuned Physical Percussion Synthesizer.
 *
 * Pure mathematical 4-mode coupled-form damped sinusoidal modal resonator bank
 * + Helmholtz cavity resonance + velocity-sensitive FM/noise mallet striker.
 * Unconditionally stable at all frequencies, 0 KB extra RAM, chromatically playable across
 * all 27 keys: Handpan, Marimba, Kalimba, Tabla, Woodblocks, Gamelan, Steel Pan, IDM Glitch.
 *
 *   K1 (BLUE)   PITCH   modal overtone stretch (warm wood <-> pure <-> metallic bell)
 *   K2 (GREEN)  MALLET  striker hardness & attack click (soft felt -> hard brass/wood)
 *   K3 (WHITE)  RING    modal resonance decay & sustain bloom
 *   K4 (ORANGE) SPACE   stereo modal spread & resonant acoustic cavity body */
#include "internal.h"
#include "dsp.h"

typedef struct ModalAlgo {
    f32 rat[4];      /* 4 modal frequency ratios relative to fundamental */
    f32 amp[4];      /* relative mode amplitudes */
    f32 dec[4];      /* base mode decay times (seconds) */
    f32 helm;        /* Helmholtz low-cavity ratio (0 = none) */
    f32 h_amp;       /* Helmholtz level */
    f32 bend_st;     /* initial pitch bend in semitones (Tabla upward pressure bend!) */
    f32 fm_click;    /* mallet FM snap ratio */
} ModalAlgo;

static const ModalAlgo MA[NALGO] = {
    /* 0 HANDPAN */ {{1.00f, 2.00f, 3.00f, 4.02f},  {1.00f, 0.56f, 0.34f, 0.16f}, {2.40f, 1.50f, 0.95f, 0.45f}, 0.50f, 0.36f,  0.0f, 2.5f},
    /* 1 MARIMBA */ {{1.00f, 3.98f, 9.20f, 1.003f}, {1.00f, 0.42f, 0.16f, 0.38f}, {0.95f, 0.18f, 0.06f, 0.85f}, 0.00f, 0.00f,  0.0f, 3.1f},
    /* 2 KALIMBA */ {{1.00f, 2.86f, 6.24f, 1.006f}, {1.00f, 0.38f, 0.24f, 0.42f}, {1.45f, 0.42f, 0.14f, 1.35f}, 0.50f, 0.20f,  0.0f, 4.4f},
    /* 3 TABLA   */ {{1.00f, 1.48f, 1.98f, 2.52f},  {1.00f, 0.58f, 0.46f, 0.28f}, {0.82f, 0.52f, 0.35f, 0.22f}, 0.50f, 0.26f, -3.2f, 2.8f},
    /* 4 WOOD    */ {{1.00f, 2.58f, 4.16f, 6.32f},  {1.00f, 0.52f, 0.28f, 0.15f}, {0.26f, 0.12f, 0.06f, 0.04f}, 0.00f, 0.00f,  0.0f, 3.7f},
    /* 5 GAMELAN */ {{1.00f, 1.007f, 2.756f, 5.40f},{0.90f, 0.86f, 0.45f, 0.25f}, {3.20f, 3.10f, 1.35f, 0.55f}, 0.00f, 0.00f,  0.0f, 2.7f},
    /* 6 STEEL   */ {{1.00f, 2.00f, 2.99f, 5.04f},  {0.92f, 0.72f, 0.48f, 0.26f}, {1.80f, 1.40f, 0.88f, 0.38f}, 0.00f, 0.00f,  0.0f, 4.0f},
    /* 7 GLITCH  */ {{1.00f, 1.732f, 3.317f, 7.14f},{0.85f, 0.75f, 0.55f, 0.42f}, {0.42f, 0.24f, 0.14f, 0.08f}, 0.00f, 0.00f,  9.5f, 5.9f},
};

static struct {
    int algo;
    f32 stretch, mallet, ring, space;
} PR;

static void perc_block(const Blk *b)
{
    PR.algo    = b->algo & 7;
    PR.stretch = b->k[0];
    PR.mallet  = b->k[1];
    PR.ring    = b->k[2];
    PR.space   = b->k[3];
}

static void perc_note(Voice *v, const Blk *b)
{
    int i;
    (void)b;
    if (!v->u[0]) v->u[0] = 0x9E2C0001u + (uint32_t)v->idx * 7919u;
    if (v->env < 0.001f) {
        for (i = 0; i < 16; i++) v->s[i] = 0.0f;
        for (i = 0; i < 6; i++) v->ph[i] = 0.0f;
    } else {
        /* Keep slight natural sympathetic ring on re-strike */
        for (i = 0; i < 10; i++) v->s[i] *= 0.35f;
    }
    v->s[10] = 1.0f;   /* mallet impulse envelope */
    v->s[11] = 1.0f;   /* pitch bend envelope (Tabla / Glitch) */
    v->s[12] = 0.0f;   /* mallet LPF state */
    v->u[1]  = 0u;     /* sample counter since strike */
}

static void perc_render(Voice *v, const Blk *b, f32 *out_l, f32 *out_r, int n)
{
    const ModalAlgo *a = &MA[PR.algo];
    f32 pitch = v->pitch + b->bend;
    f32 vel = lerpf(1.0f, 0.28f + 0.92f * v->vel_f, b->vel_sens);
    f32 stretch = exp2fx((PR.stretch - 0.5f) * 1.15f);   /* 0.67x .. 1.49x upper-mode stretch */
    f32 ring_mul = exp2fx((PR.ring - 0.5f) * 3.8f);      /* 0.27x .. 3.7x decay scale */
    f32 wid = 0.12f + 0.88f * PR.space;
    f32 bend_e = v->s[11];
    f32 f0, mc[4], ms[4], ma[4], hc = 0.0f, hs = 0.0f;
    f32 mal_env = v->s[10], mal_lp = v->s[12];
    f32 mal_dec = exp2fx(-1.4427f / ((0.0018f + 0.011f * (1.0f - PR.mallet)) * b->sr));
    f32 mal_cut = 0.10f + 0.85f * PR.mallet;
    f32 d_fm0, d_fm1, p0 = v->ph[0], p1 = v->ph[1];
    uint32_t rng = v->u[0], scnt = v->u[1];
    int m, i;

    if (a->bend_st != 0.0f)
        pitch += a->bend_st * bend_e * (0.45f + 0.85f * PR.mallet);
    if (b->punch > 0.01f && a->bend_st == 0.0f)
        pitch += b->punch * bend_e * 0.9f;

    f0 = clampf(mtof(pitch), 32.0f, 4500.0f);
    d_fm0 = minf(f0 * b->isr, 0.44f);
    d_fm1 = minf(f0 * a->fm_click * b->isr, 0.44f);

    /* Precompute exact coupled-form rotation coefficients (cos, sin) * r for 4 modes */
    for (m = 0; m < 4; m++) {
        f32 rat = (m == 0) ? a->rat[0] : (1.0f + (a->rat[m] - 1.0f) * stretch);
        f32 fm = f0 * rat;
        if (fm >= 0.43f * b->sr) {
            mc[m] = ms[m] = ma[m] = 0.0f;
        } else {
            f32 t60 = maxf(0.020f, a->dec[m] * ring_mul * clampf(exp2fx((60.0f - pitch) * (1.0f / 42.0f)), 0.45f, 1.65f));
            f32 r = clampf(exp2fx(-10.0f / (t60 * b->sr)), 0.85f, 0.99990f);
            f32 w = fm * b->isr;
            mc[m] = r * tcos(w);
            ms[m] = r * tsin(w);
            /* Mallet hardness scales upper mode excitation! */
            ma[m] = a->amp[m] * (m == 0 ? 1.0f : (0.28f + 1.25f * PR.mallet * vel));
        }
    }
    if (a->helm > 0.0f) {
        f32 fh = clampf(f0 * a->helm, 38.0f, 900.0f);
        f32 r = clampf(exp2fx(-10.0f / (0.35f * ring_mul * b->sr)), 0.90f, 0.9996f);
        f32 w = fh * b->isr;
        hc = r * tcos(w);
        hs = r * tsin(w);
    }

    for (i = 0; i < n; i++) {
        f32 nz = noise_s(&rng);
        f32 exc = 0.0f, sl = 0.0f, sr = 0.0f;
        f32 ml_l, ml_r;

        /* Synthesized mallet strike impulse (filtered burst + FM click) */
        if (mal_env > 0.0005f) {
            f32 fm_k = tsin(p0 + tsin(p1) * (0.4f + 2.2f * PR.mallet));
            mal_lp += mal_cut * (nz - mal_lp);
            exc = (mal_lp * (0.55f + 0.45f * PR.mallet) + fm_k * (0.35f + 0.55f * PR.mallet)) * mal_env * vel * 0.24f;
            if (PR.algo == 7 && (scnt & 31u) == 0u) exc *= 1.85f; /* GLITCH micro-burst */
            mal_env *= mal_dec;
        }

        /* Tick 4 coupled-form modal resonators with wide stereo mode & register panning */
        {
            static const f32 MPAN[4] = {-0.38f, 0.72f, -0.84f, 0.92f};
            for (m = 0; m < 4; m++) {
                f32 x = v->s[2 * m], y = v->s[2 * m + 1];
                f32 nx = mc[m] * x - ms[m] * y + exc * ma[m];
                f32 ny = ms[m] * x + mc[m] * y;
                f32 out_m = ny;
                f32 pan = MPAN[m] * wid;
                v->s[2 * m]     = nx;
                v->s[2 * m + 1] = ny;
                sl += out_m * (1.0f - pan * 0.62f);
                sr += out_m * (1.0f + pan * 0.62f);
            }
        }

        /* Optional Helmholtz acoustic cavity resonance (Handpan / Kalimba / Tabla) */
        if (a->helm > 0.0f) {
            f32 hx = v->s[8], hy = v->s[9];
            f32 nhx = hc * hx - hs * hy + exc * a->h_amp * (0.45f + 0.95f * PR.space);
            f32 nhy = hs * hx + hc * hy;
            v->s[8] = nhx;
            v->s[9] = nhy;
            sl += nhy;
            sr += nhy;
        }

        /* Direct subtle acoustic mallet knock */
        ml_l = exc * (0.35f + 0.65f * PR.mallet);
        ml_r = ml_l * (0.92f + 0.08f * PR.space);

        out_l[i] = softclip((sl + ml_l) * 1.15f);
        out_r[i] = softclip((sr + ml_r) * 1.15f);

        p0 += d_fm0; if (p0 >= 1.0f) p0 -= 1.0f;
        p1 += d_fm1; if (p1 >= 1.0f) p1 -= 1.0f;
        scnt++;
    }

    v->u[0] = rng; v->u[1] = scnt;
    v->ph[0] = p0; v->ph[1] = p1;
    v->s[10] = mal_env; v->s[11] *= 1.0f - onepole_k(PR.algo == 7 ? 0.018f : 0.075f, b->sr / (f32)n);
    v->s[12] = mal_lp;
}

/* ---- 4-Color Vector Picture: Handpan / Modal Resonant Drum & Chladni Ripples ----- */
static f32 pr_rip[4], pr_rx[4], pr_ry[4], pr_dimp[8], pr_lev;
static int pr_ridx;
static uint32_t pr_seen;

static void perc_draw(const Vis *vis)
{
    f32 dt = vis->dt, stretch = vis->k[0], mallet = vis->k[1], ring = vis->k[2], space = vis->k[3];
    f32 cx = 120.0f, cy = 114.0f, body_r = 62.0f + 10.0f * space;
    static const int RC[4] = {GC_BLUE, GC_GREEN, GC_ORANGE, GC_GOLD};
    int algo = vis->algo & 7, i;

    if (vis->fresh) {
        pr_seen = vis->notes;
        pr_lev = 0.0f;
        pr_ridx = 0;
        for (i = 0; i < 4; i++) { pr_rip[i] = 1.0f; pr_rx[i] = cx; pr_ry[i] = cy; }
        for (i = 0; i < 8; i++) pr_dimp[i] = 0.0f;
    }
    if (vis->notes != pr_seen) {
        int dimple = ((int)vis->last_pitch) & 7;
        f32 th = (f32)dimple * (1.0f / 8.0f) - 0.25f;
        pr_seen = vis->notes;
        pr_dimp[dimple] = 1.0f;
        pr_rip[pr_ridx] = 0.0f;
        pr_rx[pr_ridx] = cx + cos_turns(th) * (body_r * 0.58f);
        pr_ry[pr_ridx] = cy + sin_turns(th) * (body_r * 0.58f);
        pr_ridx = (pr_ridx + 1) & 3;
    }
    pr_lev += (minf(1.0f, vis->level * 3.8f) - pr_lev) * minf(1.0f, dt * 16.0f);

    /* Acoustic cavity aura glow (Orange = K4 SPACE) */
    g_color(GC_ORANGE);
    g_glow(cx, cy, 38.0f + 24.0f * space, 0.20f + 0.25f * space * 0.7f);

    /* Outer acoustic shell & Chladni modal standing-wave petal ring */
    g_color(GC_SLATE);
    g_ring(cx, cy, body_r + 8.0f, 1.2f, 0.25f + 0.35f * space);
    {
        f32 xy[2 * 64];
        f32 petals = 3.0f + (f32)((algo + (int)(stretch * 4.0f)) % 6);
        f32 amp = (3.5f + 7.5f * pr_lev + 3.0f * mallet) * (0.5f + 0.5f * stretch);
        for (i = 0; i < 64; i++) {
            f32 th = (f32)i * (1.0f / 64.0f);
            f32 r = body_r * 0.84f + amp * cos_turns(petals * th + vis->t * 0.35f);
            xy[2 * i]     = cx + cos_turns(th) * r;
            xy[2 * i + 1] = cy + sin_turns(th) * r;
        }
        g_color(GC_CYAN);
        g_poly(xy, 64, 1, 1.4f, 0.36f + 0.42f * pr_lev);
    }
    g_color(GC_WHITE);
    g_ring(cx, cy, body_r, 2.2f, 0.88f);

    /* Center Gu / Helmholtz dome (Blue = K1 PITCH stretch) */
    g_color(GC_BLUE);
    g_ring(cx, cy, 13.0f + 7.0f * stretch + 4.0f * pr_lev, 2.0f, 0.78f + 0.22f * pr_lev);
    g_disc(cx, cy, 3.8f + 3.8f * mallet + 2.5f * pr_lev, 0.90f);

    /* 8 tonal dimples around the Handpan shell with active strike glow & resonance spokes */
    for (i = 0; i < 8; i++) {
        f32 th = (f32)i * (1.0f / 8.0f) - 0.25f;
        f32 dx = cx + cos_turns(th) * (body_r * 0.58f);
        f32 dy = cy + sin_turns(th) * (body_r * 0.58f);
        f32 dr = 9.0f + 2.5f * (1.0f - (f32)(i & 3) * 0.2f);
        f32 de = pr_dimp[i];
        pr_dimp[i] = maxf(0.0f, de - dt / (0.18f + 0.75f * ring));
        if (de > 0.04f) {
            g_color(RC[i & 3]);
            g_line(cx, cy, dx, dy, 1.3f, 0.45f * de);
            g_disc(dx, dy, dr * 0.72f * de, 0.80f * de);
        }
        g_color(de > 0.1f ? RC[i & 3] : ((i & 1) ? GC_CYAN : GC_SLATE));
        g_ring(dx, dy, dr + 2.0f * de, 1.5f + 0.8f * de, 0.55f + 0.45f * de);
    }

    /* Animated expanding Chladni modal strike ripples */
    for (i = 0; i < 4; i++) {
        if (pr_rip[i] < 1.0f) {
            f32 r = 6.0f + pr_rip[i] * (38.0f + 32.0f * ring);
            f32 a = (1.0f - pr_rip[i]) * (0.65f + 0.35f * mallet);
            g_color(RC[i]);
            g_ring(pr_rx[i], pr_ry[i], r, 2.2f * (1.0f - 0.5f * pr_rip[i]), a);
            pr_rip[i] = minf(1.0f, pr_rip[i] + dt * (1.6f - 1.1f * ring));
        }
    }
    g_color(GC_WHITE);
}

const InstDef inst_perc = {
    "PERC",
    {255, 194, 82},
    {"PITCH", "MALLET", "RING", "SPACE"},
    {"HANDPAN", "MARIMBA", "KALIMBA", "TABLA", "WOOD", "GAMELAN", "STEEL", "GLITCH"},
    {
        {50, 52, 58, 52},            /* K1..K4 defaults */
        0,                           /* algo: HANDPAN */
        {0, 60, 48, 52},             /* natural percussive ring Amp ADSR */
        {45, 30, 82, 22},            /* expressive velocity & strike */
        {0, 32, 0, 0},               /* LFO */
        {0, 3, 2, 65},               /* Arp */
        {0, 50, 46}                  /* Mix */
    },
    0.29f,
    perc_block, perc_note, perc_render, 0, perc_draw, 0
};
