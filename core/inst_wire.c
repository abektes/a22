/* A22 - WIRE: FM-Excited Physical Karplus-Strong Stereo Waveguide.
 *
 * Pure physical modeling with ZERO sample ROM: precision fractional-delay waveguide
 * (1st-order Thiran allpass tuning accurate to < 1 cent up to C7) stored in compact int16
 * .pool RAM (32 KB total for all 8 polyphonic strings), excited by synthesized FM mallet,
 * comb-notched plectrum, or continuous rosin bow friction, plus stereo dual-pickup tapping
 * and acoustic soundboard body resonance.
 *
 *   K1 (BLUE)   DAMP     string damping & sustain brightness (muted nylon -> ringing steel)
 *   K2 (GREEN)  PLUCK    pick position & FM exciter brightness (soft thumb -> metal pick)
 *   K3 (WHITE)  BODY     acoustic wooden soundboard resonance & stereo pickup width
 *   K4 (ORANGE) TENSION  inharmonic string stiffness / 12-string shimmer / sitar buzz */
#include "internal.h"
#include "dsp.h"

#define KS_MAX  2048u
#define KS_MASK (KS_MAX - 1u)

/* 8 voices x 2048 int16 = 32 KB in .pool (each voice exclusively owns row v->idx -> dual-core safe!) */
static int16_t ks_buf[NVOICE][KS_MAX] H1_POOL;

typedef struct StrAlgo {
    f32 dec;        /* base feedback decay closeness to 1 (higher = longer ring) */
    f32 bright;     /* base exciter & damping brightness */
    f32 fm_rat;     /* FM mallet exciter ratio */
    f32 body_hz;    /* acoustic chamber resonance Hz */
    f32 buzz;       /* sitar jawari buzz / nonlinearity */
    int8_t oct;     /* octave shift */
    uint8_t bowed;  /* 1 = continuous bow excitation while gated */
} StrAlgo;

static const StrAlgo SA[NALGO] = {
    /* 0 NYLON */ {0.9945f, 0.52f, 2.00f, 210.0f, 0.00f,  0, 0},
    /* 1 STEEL */ {0.9970f, 0.82f, 3.00f, 250.0f, 0.04f,  0, 0},
    /* 2 HARP  */ {0.9978f, 0.72f, 2.50f, 320.0f, 0.00f,  0, 0},
    /* 3 KOTO  */ {0.9958f, 0.88f, 3.76f, 440.0f, 0.08f,  0, 0},
    /* 4 CLAV  */ {0.9920f, 0.92f, 4.00f, 520.0f, 0.14f,  0, 0},
    /* 5 BASS  */ {0.9942f, 0.42f, 1.50f, 130.0f, 0.02f, -1, 0},
    /* 6 SITAR */ {0.9968f, 0.86f, 2.76f, 380.0f, 0.48f,  0, 0},
    /* 7 BOWED */ {0.9955f, 0.65f, 2.00f, 260.0f, 0.05f,  0, 1},
};

static struct {
    int algo;
    f32 damp, pluck, body, tens;
} ST;

static void string_block(const Blk *b)
{
    ST.algo  = b->algo & 7;
    ST.damp  = b->k[0];
    ST.pluck = b->k[1];
    ST.body  = b->k[2];
    ST.tens  = b->k[3];
}

static void string_note(Voice *v, const Blk *b)
{
    const StrAlgo *a = &SA[ST.algo];
    int16_t *ks = ks_buf[v->idx & 7];
    f32 f = mtof(v->pitch + (f32)(a->oct * 12));
    uint32_t period = (uint32_t)iclamp((int)(b->sr / maxf(f, 24.0f)), 24, 900);
    uint32_t i;

    if (!v->u[0]) v->u[0] = 0x57A10001u + (uint32_t)v->idx * 4099u;
    if (v->env < 0.001f) {
        for (i = 0; i < KS_MAX; i++) ks[i] = 0;
        for (i = 0; i < 16; i++) v->s[i] = 0.0f;
        for (i = 0; i < 6; i++) v->ph[i] = 0.0f;
        v->u[1] = 0;
    } else {
        /* Dampen residual string slightly for natural legato re-pluck */
        for (i = 0; i < KS_MAX; i++) ks[i] = (int16_t)((int32_t)ks[i] >> 2);
    }
    v->u[2] = a->bowed ? 128u : period;      /* exciter burst countdown */
    v->u[3] = v->u[2];                       /* exciter total length */
    v->s[6] = (ST.algo == 3) ? 1.0f : 0.0f;  /* koto pitch-bend pluck release */
    v->s[7] = 0.0f;                          /* bow ramp */
}

static void string_render(Voice *v, const Blk *b, f32 *out_l, f32 *out_r, int n)
{
    const StrAlgo *a = &SA[ST.algo];
    int16_t *ks = ks_buf[v->idx & 7];
    f32 pitch = v->pitch + (f32)(a->oct * 12) + b->bend;
    f32 vel = lerpf(1.0f, 0.30f + 0.90f * v->vel_f, b->vel_sens);
    f32 f, D_tot, D, frac, eta;
    uint32_t di, wp = v->u[1], exc_left = v->u[2], exc_len = v->u[3] ? v->u[3] : 1u;
    uint32_t rng = v->u[0];
    f32 lp_prev = v->s[0], ap_y = v->s[1], ap_x = v->s[2], dc_x = v->s[3], dc_y = v->s[4], ex_lp = v->s[5];
    f32 p0 = v->ph[0], p1 = v->ph[1], p_sym = v->ph[2];
    /* 2-tap FIR damping filter coefficient a in [0.06, 0.46] -> exact phase delay = a samples */
    f32 bright = clampf(a->bright * (0.45f + 0.90f * ST.damp) * lerpf(1.0f, 0.55f + 0.65f * v->menv, b->menv_amt * 0.45f), 0.08f, 0.96f);
    f32 damp_a = clampf(0.46f - 0.39f * bright, 0.06f, 0.46f);
    /* Frequency-compensated decay gain: keep high notes ringing naturally! */
    f32 fb_g;
    f32 d_fm0, d_fm1, d_sym;
    f32 buzz = a->buzz + ST.tens * (ST.algo == 6 ? 0.42f : 0.10f);
    f32 body_mix = 0.12f + 0.55f * ST.body;
    uint32_t tap_off_l, tap_off_r, pick_comb;
    Svf body_f;
    int i;

    if (ST.algo == 3 && v->s[6] > 0.001f)
        pitch += v->s[6] * (0.45f + 1.1f * ST.tens);  /* Koto initial downward string snap */

    f = clampf(mtof(pitch), 24.0f, 5200.0f);
    D_tot = b->sr / f;
    D = clampf(D_tot - damp_a, 3.5f, (f32)(KS_MAX - 10u));
    di = (uint32_t)(int)(D - 0.3f);
    frac = D - (f32)di;
    /* 1st-order Thiran allpass: frac in [0.3, 1.3) -> eta in (-0.13, 0.54], unconditionally stable */
    eta = (1.0f - frac) / (1.0f + frac);

    /* Decay per round trip compensated for pitch (rich full-bodied sustain) */
    {
        f32 t60 = (1.1f + 7.2f * ST.damp * ST.damp) * (a->dec > 0.996f ? 1.45f : 1.0f);
        f32 per_loop = exp2fx(-10.0f / maxf(t60 * f, 4.0f));
        fb_g = clampf(per_loop, 0.94f, 0.9990f);
        if (!v->gate && a->bowed) fb_g = minf(fb_g, 0.993f);
    }

    d_fm0 = minf(f * b->isr, 0.44f);
    d_fm1 = minf(f * a->fm_rat * b->isr, 0.44f);
    d_sym = minf(f * (1.0f + 0.0035f + 0.009f * ST.tens) * (ST.algo == 1 ? 2.0f : 1.0f) * b->isr, 0.44f);

    /* Dual stereo pickups & pick-position comb delay */
    tap_off_l = (uint32_t)iclamp((int)(D * 0.22f), 1, (int)(KS_MAX - 2u));
    tap_off_r = (uint32_t)iclamp((int)(D * 0.67f), 1, (int)(KS_MAX - 2u));
    pick_comb = (uint32_t)iclamp((int)(D * (0.08f + 0.38f * ST.pluck)), 1, (int)(KS_MAX - 2u));

    svf_set(&body_f, clampf(a->body_hz * (0.75f + 0.65f * ST.body), 90.0f, 1800.0f), 1.9f + 1.8f * ST.body, b->isr);

    for (i = 0; i < n; i++) {
        f32 exc = 0.0f;
        f32 r_raw, ap, y, y_dc, tl, tr, bl, br;

        /* 1) Synthesized FM + noise pluck / bow exciter */
        if (exc_left > 0u) {
            f32 u = (f32)exc_left / (f32)exc_len;         /* 1 -> 0 across burst */
            f32 win = 4.0f * u * (1.0f - u);              /* smooth parabolic envelope */
            f32 nz = noise_s(&rng);
            f32 fm_hit = tsin(p0 + tsin(p1) * (0.4f + 1.5f * ST.pluck));
            f32 ex_cut = 0.14f + 0.72f * ST.pluck;
            ex_lp += ex_cut * (nz - ex_lp);
            exc = (ex_lp * (0.42f + 0.32f * ST.pluck) + fm_hit * (0.68f + 0.35f * ST.pluck)) * win * vel * 0.92f;
            exc_left--;
        }
        if (a->bowed && v->gate) {
            f32 bow_r = v->s[7];
            f32 nz = noise_s(&rng);
            bow_r += (1.0f - bow_r) * 0.00055f;
            v->s[7] = bow_r;
            ex_lp += (0.08f + 0.28f * ST.pluck) * (nz - ex_lp);
            /* Nonlinear stick-slip bow drive coupled to string fundamental */
            exc += (ex_lp * 0.16f + tsin(p0 + 0.55f * tsin(p1)) * 0.055f) * bow_r * vel * (0.45f + 0.75f * ST.pluck);
        }

        /* 2) Read fractional delay line with Thiran allpass */
        r_raw = (f32)ks[(wp - di) & KS_MASK] * (1.0f / 16384.0f);
        ap = eta * (r_raw - ap_y) + ap_x;
        ap_x = r_raw;
        ap_y = ap;

        /* 3) 2-tap FIR string damping (exact phase delay = damp_a) */
        y = (1.0f - damp_a) * ap + damp_a * lp_prev;
        lp_prev = ap;

        /* 4) Optional sitar jawari bridge buzz / harmonic nonlinearity */
        if (buzz > 0.01f)
            y = lerpf(y, softclip(y * (1.0f + 2.2f * buzz) + 0.12f * buzz * y * y), buzz * 0.45f);

        /* 5) Feedback gain + lossless-in-passband DC blocker + safety clamp */
        y = y * fb_g + exc;
        y_dc = y - dc_x + 0.9990f * dc_y;
        dc_x = y;
        dc_y = y_dc;
        if (absf(dc_y) < 1e-12f) dc_y = 0.0f;
        y = clampf(y_dc, -1.85f, 1.85f);

        ks[wp] = (int16_t)clampf(y * 16384.0f, -32700.0f, 32700.0f);

        /* 6) Dual stereo pickup tapping + direct plectrum transient + pick-position comb notch + sympathetic shimmer */
        tl = (f32)ks[(wp - tap_off_l) & KS_MASK] * (1.0f / 16384.0f) + exc * 0.24f;
        tr = (f32)ks[(wp - tap_off_r) & KS_MASK] * (1.0f / 16384.0f) + exc * 0.20f;
        {
            f32 notch = (f32)ks[(wp - pick_comb) & KS_MASK] * (1.0f / 16384.0f);
            f32 p_mix = 0.25f * ST.pluck;
            tl = tl - notch * p_mix;
            tr = tr - notch * p_mix * 0.7f;
        }
        if (ST.tens > 0.05f) {
            f32 sym = tsin(p_sym) * absf(y) * ST.tens * (ST.algo == 1 ? 0.55f : 0.30f);
            tl += sym;
            tr -= sym * 0.85f;
        }

        /* 7) Acoustic soundboard body resonator (stereo bandpass + low-end wooden cavity thump) */
        {
            f32 lpl, lpr;
            SVF_TICK(body_f, &v->s[8],  tl, lpl, bl);
            SVF_TICK(body_f, &v->s[10], tr, lpr, br);
            {
                f32 wid = 0.20f + 0.80f * ST.body;
                f32 xl = tl * (1.0f - body_mix * 0.35f) + (bl * 1.15f + lpl * 0.24f) * body_mix;
                f32 xr = tr * (1.0f - body_mix * 0.35f) + (br * 1.15f + lpr * 0.24f) * body_mix;
                f32 mid = 0.5f * (xl + xr), side = 0.5f * (xl - xr) * wid;
                out_l[i] = mid + side;
                out_r[i] = mid - side;
            }
        }

        wp = (wp + 1u) & KS_MASK;
        p0 += d_fm0; if (p0 >= 1.0f) p0 -= 1.0f;
        p1 += d_fm1; if (p1 >= 1.0f) p1 -= 1.0f;
        p_sym += d_sym; if (p_sym >= 1.0f) p_sym -= 1.0f;
    }

    v->u[0] = rng; v->u[1] = wp; v->u[2] = exc_left;
    v->s[0] = lp_prev; v->s[1] = ap_y; v->s[2] = ap_x; v->s[3] = dc_x; v->s[4] = dc_y; v->s[5] = ex_lp;
    v->s[6] *= 1.0f - onepole_k(0.055f, b->sr / (f32)n);
    v->ph[0] = p0; v->ph[1] = p1; v->ph[2] = p_sym;
}

/* ---- 4-Color Vector Picture: Physical Vibrating 6-String Harp & Soundhole -------- */
static f32 st_ph, st_vib[6], st_lev;
static uint32_t st_seen;

static void string_draw(const Vis *vis)
{
    f32 dt = vis->dt, damp = vis->k[0], pluck = vis->k[1], body = vis->k[2], tens = vis->k[3];
    f32 x0 = 26.0f, x1 = 214.0f, cx = 120.0f, cy = 114.0f;
    f32 pick_x = lerpf(48.0f, 192.0f, pluck);
    int algo = vis->algo & 7, s, i;
    static const int SC[6] = {GC_BLUE, GC_CYAN, GC_GREEN, GC_WHITE, GC_GOLD, GC_ORANGE};

    if (vis->fresh) {
        st_seen = vis->notes;
        st_lev = 0.0f;
        for (s = 0; s < 6; s++) st_vib[s] = 0.0f;
    }
    if (vis->notes != st_seen) {
        int idx = ((int)vis->last_pitch) % 6;
        if (idx < 0) idx += 6;
        st_seen = vis->notes;
        st_vib[idx] = 1.0f;
    }
    st_lev += (minf(1.0f, vis->level * 3.8f) - st_lev) * minf(1.0f, dt * 16.0f);
    st_ph = fractf(st_ph + dt * (5.5f + 7.5f * tens));

    /* Acoustic soundboard soundhole glow & rosette rings (White/Gold = K3 BODY) */
    g_color(GC_ORANGE);
    g_glow(cx, cy, 36.0f + 22.0f * body + 12.0f * st_lev, 0.20f + 0.26f * body);
    g_color(GC_SLATE);
    g_ring(cx, cy, 48.0f + 16.0f * body, 1.3f, 0.35f + 0.30f * body);
    g_ring(cx, cy, 41.0f + 15.0f * body, 1.0f, 0.24f + 0.22f * body);
    g_color(GC_GOLD);
    g_ring(cx, cy, 34.0f + 14.0f * body + 8.0f * st_lev, 1.8f, 0.30f + 0.45f * body + 0.25f * st_lev);

    /* Nut & bridge posts (SITAR gets curved jawari bridge on right) */
    g_color(GC_WHITE);
    g_line(x0, 50.0f, x0, 178.0f, 2.4f, 0.85f);
    if (algo == 6) {
        g_color(GC_ORANGE);
        g_arc(x1 - 8.0f, cy, 66.0f, -0.18f, 0.18f, 2.6f, 0.90f);
    } else {
        g_line(x1, 50.0f, x1, 178.0f, 2.4f, 0.85f);
    }

    /* Pick position vertical guide or Bow hair ribbon (Green = K2 PLUCK) */
    g_color(GC_GREEN);
    if (algo == 7) {
        f32 bow_x = pick_x + sin_turns(vis->t * 0.65f) * (10.0f + 12.0f * st_lev);
        g_box(bow_x - 2.5f, 44.0f, 5.0f, 140.0f, 1.4f, 0.55f + 0.35f * st_lev);
    } else {
        g_line(pick_x, 46.0f, pick_x, 182.0f, 1.2f, 0.45f);
        g_disc(pick_x, 46.0f, 3.5f, 0.85f);
    }

    /* 6 vibrating physical strings (+ movable Koto ji bridges on algo 3) */
    for (s = 0; s < 6; s++) {
        f32 sy = 62.0f + (f32)s * 20.8f;
        f32 xy[2 * 48];
        f32 decay = 1.0f - minf(0.95f, dt * (1.2f + 4.0f * (1.0f - damp)));
        f32 v_amp;
        st_vib[s] *= decay;
        v_amp = (st_vib[s] * 11.5f + st_lev * 5.0f) * (0.55f + 0.45f * damp);
        for (i = 0; i < 48; i++) {
            f32 u = (f32)i * (1.0f / 47.0f);
            f32 env_x = sin_turns(u * 0.5f);
            f32 mode1 = sin_turns(st_ph * (1.0f + 0.14f * (f32)s) + (f32)s * 0.17f);
            f32 mode2 = tens * 0.45f * sin_turns(u) * sin_turns(st_ph * 2.1f);
            xy[2 * i]     = lerpf(x0, x1, u);
            xy[2 * i + 1] = sy + (mode1 * env_x + mode2) * v_amp;
        }
        if (algo == 3) {
            /* KOTO movable triangular ji bridge per string */
            f32 jx = 76.0f + (f32)s * 18.0f + tens * 16.0f;
            f32 tri[6] = {jx, sy, jx - 5.0f, sy + 8.0f, jx + 5.0f, sy + 8.0f};
            g_color(GC_GOLD);
            g_poly(tri, 3, 1, 1.4f, 0.72f);
        }
        g_color(SC[s]);
        g_poly(xy, 48, 0, 1.6f + 1.4f * st_vib[s], 0.52f + 0.46f * maxf(st_vib[s], st_lev));
        if (st_vib[s] > 0.08f) {
            g_color(GC_WHITE);
            g_disc(pick_x, sy, 2.2f + 2.5f * st_vib[s], st_vib[s]);
        }
    }
    g_color(GC_WHITE);
}

const InstDef inst_wire = {
    "WIRE",
    {255, 210, 74},
    {"DAMP", "PLUCK", "BODY", "TENSION"},
    {"NYLON", "STEEL", "HARP", "KOTO", "CLAV", "BASS", "SITAR", "BOWED"},
    {
        {64, 54, 48, 24},            /* K1..K4 defaults */
        0,                           /* algo: NYLON */
        {0, 56, 75, 42},             /* natural string Amp ADSR */
        {45, 32, 78, 12},            /* Mod/Filter Env */
        {0, 35, 0, 0},               /* LFO */
        {0, 3, 2, 65},               /* Arp */
        {0, 50, 44}                  /* Mix */
    },
    0.42f,
    string_block, string_note, string_render, 0, string_draw, 0
};
