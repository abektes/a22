/* A22 - MONO: Expressive Mono/Legato FM Lead & Acid Bass.
 *
 * True monophonic / legato priority engine with per-engine K3 GLIDE portamento,
 * 2x oversampled exponential FM bite & feedback growl, centered chest-rattling sub
 * oscillator, stereo detuned Reese/Hoover upper harmonics, asymmetric analog overdrive,
 * and a squelchy 24 dB/oct ZDF saturated analog Ladder Filter.
 *
 *   K1 (BLUE)   FM-BITE  exponential FM punch & snarling modulator depth
 *   K2 (GREEN)  CUTOFF   24 dB saturated Ladder filter cutoff (Mod-Env + Vel tracked)
 *   K3 (WHITE)  GLIDE    portamento slide time (snappy -> liquid 303 slide)
 *   K4 (ORANGE) SUB-DRV  sub-octave weight, filter resonance & warm analog drive */
#include "internal.h"
#include "dsp.h"

static struct {
    int algo;
    f32 bite, cut, glide, subdrv;
    f32 reso, drive, pwm;
} V;
static f32 v_pwm_ph, volt_t_was;

static void mono_block(const Blk *b)
{
    if (b->time < volt_t_was) v_pwm_ph = 0.0f;
    volt_t_was = b->time;
    v_pwm_ph = fractf(v_pwm_ph + 0.55f * b->step);

    V.algo   = b->algo & 7;
    V.bite   = b->k[0];
    V.cut    = 58.0f * exp2fx(b->k[1] * 7.8f);          /* 58 Hz .. 12.9 kHz */
    V.glide  = b->k[2];
    V.subdrv = b->k[3];
    V.reso   = (V.algo == 0 ? 1.55f : 0.40f) + V.subdrv * (V.algo == 0 ? 2.15f : 2.95f);
    V.drive  = 1.0f + 3.2f * V.subdrv * V.subdrv;
    V.pwm    = 0.14f * tsin(v_pwm_ph);
}

static void mono_note(Voice *v, const Blk *b)
{
    int i;
    (void)b;
    if (v->env < 0.005f) {
        /* Fresh non-legato note: snap glide pitch immediately & clean start */
        for (i = 0; i < 6; i++) v->ph[i] = 0.0f;
        for (i = 0; i < 16; i++) v->s[i] = 0.0f;
        v->s[12] = v->pitch;
        v->s[13] = 1.0f;   /* exponential pitch zap env */
    } else {
        /* Legato mono slide: keep current v->s[12] pitch and oscillator phases! */
        v->s[13] = maxf(v->s[13], 0.42f);
    }
}

static void mono_render(Voice *v, const Blk *b, f32 *out_l, f32 *out_r, int n)
{
    int algo = V.algo, i;
    f32 vel = lerpf(1.0f, 0.35f + 0.85f * v->vel_f, b->vel_sens);
    f32 menv = lerpf(1.0f, 0.20f + 1.30f * v->menv, b->menv_amt);
    f32 gk = V.glide > 0.01f ? onepole_k(0.005f * exp2fx(V.glide * 6.5f), b->sr / (f32)n) : 1.0f;
    f32 pitch, f, d0, d1, d2, dm, dsub, fc;
    f32 p0 = v->ph[0], p1 = v->ph[1], p2 = v->ph[2], pm = v->ph[3], psub = v->ph[4];
    f32 fb0 = v->s[14], fb1 = v->s[15];
    f32 zap = v->s[13];
    f32 fm_amt = (0.06f + 1.65f * V.bite * V.bite) * vel * menv;
    f32 sub_g = (algo == 5 ? 0.92f : 0.25f) + 0.65f * V.subdrv;
    f32 comp = 1.0f / (1.0f + 0.42f * (V.drive - 1.0f));
    Ladder fl;

    /* Smooth mono portamento glide controlled directly by K3 GLIDE */
    v->s[12] += (v->pitch - v->s[12]) * gk;
    pitch = v->s[12] + b->bend;

    /* Bass algorithms sit an octave lower for instant weight on standard keys */
    if (algo == 0 || algo == 1 || algo == 3 || algo == 4 || algo == 5)
        pitch -= 12.0f;

    /* Exponential pitch zap / punch transient (or Alpha Juno upward Hoover swoop) */
    if (algo == 3)      pitch += zap * (5.0f + 14.0f * V.bite);   /* ZAP-BASS punch */
    else if (algo == 6) pitch += zap * (2.5f + 9.0f * V.bite);    /* LASER sci-fi sweep */
    else if (algo == 7) pitch -= zap * (2.8f + 4.5f * V.bite);    /* HOOVER upward rave swoop */
    else if (b->punch > 0.01f) pitch += zap * b->punch * 2.2f;

    f = clampf(mtof(pitch), 22.0f, 8000.0f);
    d0   = minf(f * b->isr, 0.42f);
    d1   = minf(d0 * (1.0f + 0.0045f + 0.018f * V.bite), 0.42f);
    d2   = minf(d0 * (1.0f - 0.0042f - 0.016f * V.bite), 0.42f);
    dsub = minf(d0 * 0.5f, 0.42f);
    dm   = minf(d0 * (algo == 3 ? 2.0f : (algo == 4 ? 1.5f : (algo == 6 ? 3.0f : 1.0f))), 0.42f);

    fc = V.cut * exp2fx(v->menv * b->menv_amt * (algo == 0 ? 5.6f : 4.2f)
                      + (v->vel_f - 0.5f) * b->vel_sens * 1.8f
                      + (pitch - 48.0f) * (1.0f / 32.0f));
    ladder_set(&fl, clampf(fc, 38.0f, 16500.0f), minf(3.75f, V.reso), b->isr);

    for (i = 0; i < n; i++) {
        f32 xl = 0.0f, xr = 0.0f;
        f32 sub = tsin(psub) * sub_g;

        switch (algo) {
        case 0: /* ACID: 303 saw/square morph + snarling FM edge */
        {
            f32 mod = tsin(pm) * fm_amt * 0.35f;
            f32 sw = saw_bl(p0, d0);
            f32 sq = pulse_bl(p0, d0, 0.48f);
            f32 fm_s = tsin(p0 + mod);
            f32 s = lerpf(sw, sq, V.bite * 0.55f) * 0.72f + fm_s * 0.36f + sub * 0.40f;
            xl = s;
            xr = s * 0.96f + 0.08f * saw_bl(p1, d1);
            break;
        }
        case 1: /* REESE: massive wide stereo beating saws + tight centered sub */
        {
            f32 mod = tsin(pm) * fm_amt * 0.28f;
            f32 s1 = saw_bl(p1, d1) + 0.35f * tsin(p1 + mod);
            f32 s2 = saw_bl(p2, d2) + 0.35f * tsin(p2 - mod);
            xl = 0.60f * s1 + 0.25f * s2 + sub * 0.55f;
            xr = 0.60f * s2 + 0.25f * s1 + sub * 0.55f;
            break;
        }
        case 2: /* SH-LEAD: warm SH-101 saw + PWM + sub + expressive FM bite */
        {
            f32 pw = clampf(0.45f - 0.34f * V.bite + V.pwm, 0.08f, 0.92f);
            f32 s0 = saw_bl(p0, d0);
            f32 sq = pulse_bl(p1, d1, pw);
            f32 fm = tsin(p0 + tsin(pm) * fm_amt * 0.5f);
            xl = 0.45f * s0 + 0.38f * sq + 0.28f * fm + sub * 0.45f;
            xr = 0.40f * s0 + 0.42f * sq + 0.28f * fm + sub * 0.45f;
            break;
        }
        case 3: /* ZAP-BASS: 2x oversampled FM club/donk punch bass */
        {
            f32 s_acc = 0.0f;
            int os;
            for (os = 0; os < 2; os++) {
                f32 m = tsin(pm + 0.22f * V.bite * 0.5f * (fb0 + fb1));
                fb1 = fb0; fb0 = m;
                s_acc += 0.5f * tsin(p0 + m * (0.28f + fm_amt * (0.4f + 0.8f * zap)));
                p0 += d0 * 0.5f; if (p0 >= 1.0f) p0 -= 1.0f;
                pm += dm * 0.5f; if (pm >= 1.0f) pm -= 1.0f;
            }
            xl = s_acc * 0.85f + sub * 0.55f;
            xr = s_acc * 0.85f + sub * 0.55f;
            goto after_step;
        }
        case 4: /* GROWL: 2x oversampled high-feedback FM bass growl */
        {
            f32 sl_a = 0.0f, sr_a = 0.0f;
            f32 fbk = 0.12f + 0.38f * V.bite;
            int os;
            for (os = 0; os < 2; os++) {
                f32 m = tsin(pm + fbk * 0.5f * (fb0 + fb1));
                fb1 = fb0; fb0 = m;
                sl_a += 0.5f * tsin(p0 + m * (0.35f + fm_amt));
                sr_a += 0.5f * tsin(p1 + m * (0.35f + fm_amt * 0.94f));
                p0 += d0 * 0.5f; if (p0 >= 1.0f) p0 -= 1.0f;
                p1 += d1 * 0.5f; if (p1 >= 1.0f) p1 -= 1.0f;
                pm += dm * 0.5f; if (pm >= 1.0f) pm -= 1.0f;
            }
            xl = sl_a * 0.72f + sub * 0.58f;
            xr = sr_a * 0.72f + sub * 0.58f;
            goto after_step;
        }
        case 5: /* SUB-808: pure booming warm analog/FM sub-bass */
        {
            f32 m = tsin(pm) * fm_amt * 0.25f;
            f32 s = tsin(p0 + m) * 0.85f + 0.22f * tri_wave(p0) + sub * 0.42f;
            xl = s;
            xr = s;
            break;
        }
        case 6: /* LASER: hard-sync + exponential FM sci-fi lead */
        {
            f32 slave_d = minf(d0 * (1.2f + 5.2f * V.bite * (0.35f + 0.85f * v->menv)), 0.44f);
            p1 += slave_d; if (p1 >= 1.0f) p1 -= 1.0f;
            if (p0 + d0 >= 1.0f) p1 = 0.0f;
            {
                f32 sy = saw_bl(p1, slave_d);
                f32 fm = tsin(p0 + tsin(pm) * fm_amt * 0.75f);
                xl = 0.58f * sy + 0.45f * fm + sub * 0.28f;
                xr = 0.52f * sy + 0.50f * fm + sub * 0.28f;
            }
            p0 += d0; if (p0 >= 1.0f) p0 -= 1.0f;
            pm += dm; if (pm >= 1.0f) pm -= 1.0f;
            goto after_step;
        }
        default: /* HOOVER: thick detuned PWM + sub-square rave mono monster */
        {
            f32 pw = clampf(0.5f - 0.38f * V.bite + V.pwm, 0.08f, 0.92f);
            f32 o0 = pulse_bl(p0, d0, pw);
            f32 o1 = saw_bl(p1, d1);
            f32 o2 = saw_bl(p2, d2);
            f32 sq_sub = pulse_bl(psub, dsub, 0.5f) * sub_g;
            f32 o3 = tsin(pm) * 0.40f;
            xl = 0.42f * o0 + 0.44f * o1 + 0.22f * o2 + o3 * 0.22f + sq_sub * 0.46f;
            xr = 0.42f * o0 + 0.22f * o1 + 0.44f * o2 + o3 * 0.22f + sq_sub * 0.46f;
            break;
        }
        }

        p0 += d0; if (p0 >= 1.0f) p0 -= 1.0f;
        p1 += d1; if (p1 >= 1.0f) p1 -= 1.0f;
        p2 += d2; if (p2 >= 1.0f) p2 -= 1.0f;
        pm += dm; if (pm >= 1.0f) pm -= 1.0f;

after_step:
        psub += dsub; if (psub >= 1.0f) psub -= 1.0f;

        /* Asymmetric warm analog drive -> 24 dB/oct saturated Ladder Filter */
        xl = tape_sat(xl * V.drive) * comp;
        xr = tape_sat(xr * V.drive) * comp;
        out_l[i] = ladder_tick(&fl, &v->s[0], xl);
        out_r[i] = ladder_tick(&fl, &v->s[4], xr);
    }

    v->ph[0] = p0; v->ph[1] = p1; v->ph[2] = p2; v->ph[3] = pm; v->ph[4] = psub;
    v->s[13] *= 1.0f - onepole_k(algo == 3 ? 0.022f : (algo == 7 ? 0.11f : 0.038f), b->sr / (f32)n);
    v->s[14] = fb0; v->s[15] = fb1;
}

/* ---- 4-Color Vector Picture: Analog Voltmeter + Slew Curve + High-Voltage Arc ---- */
static f32 vt_needle, vt_needlev, vt_lev, vt_pitch_s;
static uint32_t vt_seen;

static void mono_draw(const Vis *vis)
{
    f32 dt = vis->dt, bite = vis->k[0], cut = vis->k[1], glide = vis->k[2], subdrv = vis->k[3];
    f32 xy[2 * 56], xy2[2 * 56];
    int i;

    if (vis->fresh) {
        vt_seen = vis->notes;
        vt_needle = vt_needlev = vt_lev = 0.0f;
        vt_pitch_s = 60.0f;
    }
    if (vis->notes != vt_seen) {
        vt_seen = vis->notes;
        vt_needlev += 5.5f;
    }
    vt_lev += (minf(1.0f, vis->level * 3.8f) - vt_lev) * minf(1.0f, dt * 16.0f);
    vt_pitch_s += (vis->last_pitch - vt_pitch_s) * minf(1.0f, dt * (22.0f - 18.0f * glide));
    spring_z(&vt_needle, &vt_needlev, clampf(0.18f + 0.45f * subdrv + 0.55f * vt_lev, 0.0f, 1.0f), 26.0f, 0.40f, dt);

    /* Top-left: Retro Analog Voltmeter Gauge (Orange needle = SUB-DRV + level) */
    {
        f32 gx = 68.0f, gy = 98.0f, gr = 38.0f;
        f32 nang = lerpf(-0.42f, -0.08f, clampf(vt_needle, 0.0f, 1.05f));
        g_color(GC_SLATE);
        g_box(22.0f, 44.0f, 92.0f, 64.0f, 1.4f, 0.55f);
        g_arc(gx, gy, gr, -0.43f, -0.07f, 1.6f, 0.45f);
        for (i = 0; i < 6; i++) {
            f32 ta = lerpf(-0.42f, -0.08f, (f32)i * 0.2f);
            g_line(gx + cos_turns(ta) * (gr - 4.5f), gy + sin_turns(ta) * (gr - 4.5f),
                   gx + cos_turns(ta) * (gr + 1.5f), gy + sin_turns(ta) * (gr + 1.5f), 1.1f, 0.4f);
        }
        g_color(GC_RED);
        g_arc(gx, gy, gr, -0.17f, -0.07f, 2.2f, 0.85f);
        g_color(GC_ORANGE);
        g_glow(gx, gy, 18.0f + 16.0f * subdrv, 0.18f + 0.22f * vt_lev);
        g_line(gx, gy, gx + cos_turns(nang) * (gr - 4.0f), gy + sin_turns(nang) * (gr - 4.0f), 2.2f, 0.95f);
        g_disc(gx, gy, 3.8f, 0.94f);
    }

    /* Top-right: Shaded Portamento Glide Slew Monitor (White = K3 GLIDE) */
    {
        f32 bx = 124.0f, by = 44.0f, bw = 94.0f, bh = 64.0f;
        f32 gxy[2 * 26];
        g_color(GC_SLATE);
        g_box(bx, by, bw, bh, 1.4f, 0.55f);
        gxy[0] = bx + 8.0f;
        gxy[1] = by + bh - 12.0f;
        for (i = 0; i < 24; i++) {
            f32 u = (f32)i * (1.0f / 23.0f);
            f32 k = 1.0f + (1.0f - glide) * 14.0f;
            f32 curve = u < 0.2f ? 0.0f : (1.0f - exp2fx(-(u - 0.2f) * k));
            gxy[2 * (i + 1)]     = bx + 8.0f + u * (bw - 16.0f);
            gxy[2 * (i + 1) + 1] = by + bh - 12.0f - curve * (bh - 24.0f);
        }
        gxy[2 * 25]     = bx + bw - 8.0f;
        gxy[2 * 25 + 1] = by + bh - 12.0f;

        g_color(GC_CYAN);
        g_fillv(gxy, 26, by + 12.0f, 0.28f, by + bh - 12.0f, 0.03f);
        g_color(GC_WHITE);
        g_poly(gxy + 2, 24, 0, 2.1f, 0.90f);
        g_color(GC_GREEN);
        g_line(bx + 8.0f, by + bh - 8.0f - cut * (bh - 16.0f),
               bx + bw - 8.0f, by + bh - 8.0f - cut * (bh - 16.0f), 1.3f, 0.65f);
    }

    /* Bottom: High-Voltage Plasma Arc between two terminal coils (Blue = FM-BITE, Green = CUTOFF) */
    {
        f32 x0 = 32.0f, x1 = 208.0f, cy = 152.0f, bw = 8.0f + 12.0f * vt_lev, ba = 0.14f + 0.22f * vt_lev;
        g_color(GC_CYAN);
        g_beam(x0, cy, x1, cy, bw, bw, ba, ba, 0.85f);
        for (i = 0; i < 56; i++) {
            f32 u = (f32)i * (1.0f / 55.0f);
            f32 env_x = sin_turns(u * 0.5f);
            f32 sc = vis->scope[(vis->scope_pos + i * 4) & (SCOPE_N - 1)];
            f32 zap_j = bite * 15.0f * sin_turns(u * (3.0f + 9.0f * bite) + vis->t * 6.0f);
            f32 zap_2 = (0.2f + 0.8f * bite) * 9.0f * cos_turns(u * (5.0f + 7.0f * cut) - vis->t * 4.5f);
            xy[2 * i]      = lerpf(x0, x1, u);
            xy[2 * i + 1]  = clampf(cy + (sc * 36.0f + zap_j * (0.4f + 0.85f * vt_lev)) * env_x, 116.0f, 188.0f);
            xy2[2 * i]     = lerpf(x0, x1, u);
            xy2[2 * i + 1] = clampf(cy + (sc * 44.0f - zap_2 * (0.3f + 0.7f * vt_lev)) * env_x, 116.0f, 188.0f);
        }
        g_color(GC_BLUE);
        g_poly(xy, 56, 0, 2.2f, 0.52f + 0.42f * bite);
        g_color(GC_GREEN);
        g_poly(xy2, 56, 0, 2.0f, 0.95f);

        /* Terminal posts with glowing rings */
        g_color(GC_ORANGE);
        g_ring(x0, cy, 8.5f + 2.5f * vt_lev, 1.4f, 0.55f + 0.4f * vt_lev);
        g_ring(x1, cy, 8.5f + 2.5f * vt_lev, 1.4f, 0.55f + 0.4f * vt_lev);
        g_color(GC_WHITE);
        g_disc(x0, cy, 4.8f, 0.95f);
        g_disc(x1, cy, 4.8f, 0.95f);
    }
    g_color(GC_WHITE);
}

const InstDef inst_mono = {
    "MONO",
    {255, 107, 26},
    {"FM-BITE", "CUTOFF", "GLIDE", "SUB-DRV"},
    {"ACID", "REESE", "SH-LEAD", "ZAP-BASS", "GROWL", "SUB-808", "LASER", "HOOVER"},
    {
        {42, 46, 18, 52},            /* K1..K4 defaults */
        0,                           /* algo: ACID */
        {0, 34, 65, 22},             /* tight mono Amp ADSR */
        {78, 28, 76, 35},            /* snappy Mod/Filter Env */
        {0, 42, 0, 1},               /* LFO */
        {0, 3, 1, 65},               /* Arp */
        {0, 50, 22}                  /* Mix: tight mono bass/lead send */
    },
    0.66f,
    mono_block, mono_note, mono_render, 0, mono_draw, 1
};
