/* A22 - Selectable 8-Engine FX Rack + Master Bus (3-Band EQ, Tape Drive, Glue)
 * + 48 KB RAM Tape Performance FX (Tape Brake & Beat Stutter).
 *
 * Pool RAM budget:
 *   fx_arena:    64 KB, the effect in hand's: echo_buf, 32768 x int16 (DELAY, SHIFT, CHORUS, TANK), or rev_buf, the
 *                first 24576 of the same memory (the Dattorro plate for PLATE). One effect runs at a time
 *   tape_fx_buf: 24576 x int16 = 48 KB (live Tape Stop brake & Beat Stutter loop), apart: it runs under any effect */
#include "internal.h"
#include "dsp.h"

const char *const FX_NAMES[NFX] = {
    "PLATE", "DELAY", "FILTER", "SHIFT", "CHORUS", "TANK", "LOFI", "THUMP"
};

const char *const FX_KNOBS[NFX][4] = {
    {"MIX",   "DECAY", "DAMP",   "MOD"},      /* PLATE: Dattorro modulated plate */
    {"MIX",   "TIME",  "FEEDBK", "TAPE"},     /* DELAY: stereo tape/BBD ping-pong */
    {"LPF",   "HPF",   "RESO",   "ENV"},      /* FILTER: dual resonant ZDF filter */
    {"FREQ",  "DELAY", "FEEDBK", "MIX"},      /* SHIFT: frequency shifter + comb */
    {"MIX",   "RATE",  "DEPTH",  "WIDTH"},    /* CHORUS: 3-tap stereo ensemble */
    {"MIX",   "DECAY", "TWANG",  "TONE"},     /* TANK: dispersive spring tank */
    {"CRUSH", "RATE",  "BAND",   "GRIT"},     /* LOFI: lo-fi speaker + crush */
    {"PUNCH", "DRIVE", "BASS",   "TONE"},     /* THUMP: transient + tape drive */
};

const uint8_t FX_DEFAULTS[NFX][4] = {
    {38, 62, 35, 40},   /* PLATE */
    {35, 3,  48, 30},   /* DELAY (p[1] = 0..7 division index, 3 = 1/8) */
    {78, 8,  35, 45},   /* FILTER */
    {58, 32, 55, 50},   /* SHIFT */
    {45, 32, 55, 80},   /* CHORUS */
    {40, 58, 60, 55},   /* TANK */
    {35, 30, 50, 25},   /* LOFI */
    {45, 35, 40, 55},   /* THUMP */
};

/* ---- shared .pool delay buffers ------------------------------------------------------- */
#define ECHO_N 32768u
#define REV_N  24576u
#define TFX_FRAMES 12288u
/* One effect runs in a block (fx_send_block), and one that starts again after a block without it clears its own
 * buffer first (fx_resume), so PLATE's tank and the echo line share one arena: 64 KB instead of 112 (0.6.0). Whatever
 * the other effect left there is never read. */
static union { int16_t echo[ECHO_N]; int16_t rev[REV_N]; } fx_arena H1_POOL;
#define echo_buf fx_arena.echo
#define rev_buf fx_arena.rev
static int16_t tfx_buf[TFX_FRAMES * 2u] H1_POOL;

/* ---- Dattorro Plate Reverb layout inside rev_buf (22,534 <= 24,576 int16 samples) ----- */
enum {
    D_IN1 = 142, D_IN2 = 107, D_IN3 = 379, D_IN4 = 277,
    D_AP1A = 696, D_DL1A = 4453, D_AP2A = 1800, D_DL2A = 3720,
    D_AP1B = 932, D_DL1B = 4217, D_AP2B = 2656, D_DL2B = 3163,
    D_NSEG = 12
};
/* (each segment's start in rev_buf: known when it compiles, so no table to load per read) */
enum {
    D_O0 = 0, D_O1 = D_O0 + D_IN1, D_O2 = D_O1 + D_IN2, D_O3 = D_O2 + D_IN3, D_O4 = D_O3 + D_IN4,
    D_O5 = D_O4 + D_AP1A, D_O6 = D_O5 + D_DL1A, D_O7 = D_O6 + D_AP2A, D_O8 = D_O7 + D_DL2A,
    D_O9 = D_O8 + D_AP1B, D_O10 = D_O9 + D_DL1B, D_O11 = D_O10 + D_AP2B, D_OEND = D_O11 + D_DL2B
};
typedef char d_fits[D_OEND <= REV_N ? 1 : -1];
static uint16_t d_pos[D_NSEG];
static uint32_t pl_quiet;                    /* samples the tank has been all 0 (fx_plate) */
static f32 d_inlp, d_dampa, d_dampb, d_lfo1, d_lfo2;

/* ---- DELAY / SHIFT / CHORUS / TANK state ---------------------------------------------- */
static uint32_t echo_w;
static f32 echo_t = 4000.0f, echo_tr = 6000.0f, echo_lpl, echo_lpr, echo_ol, echo_or, wow_ph, ch_ph;
static f32 shf_ph, shf_i1[4], shf_q1[4], shf_fb_l, shf_fb_r;
static f32 spr_ap[4], spr_lp;
/* DELAY, SHIFT, CHORUS and TANK asleep, as PLATE sleeps: echo_quiet counts the steps of echo_w since a sample other than
 * 0 went into echo_buf. Once it has gone round the whole line an effect reads (echo_asleep), nothing comes in and the
 * effect's own filters hold exactly 0, what it would add is exactly 0, and what it would write is 0 over 0: it only
 * moves its clocks (echo_w, its LFO, DELAY's glide to a new time), the same way, and the sound is the same to the bit. */
static uint32_t echo_quiet;
#define ECHO_QUIET_MAX (1u << 20)
static inline void echo_wrote(int16_t a, int16_t b)
{
    if (a | b) echo_quiet = 0;
    else if (echo_quiet < ECHO_QUIET_MAX) echo_quiet++;
}
#ifdef H1_DEVICE
#define echo_no_sleep 0
#define ECHO_SLEPT()
#else
static int echo_no_sleep;
static uint32_t echo_slept[NFX];
#define ECHO_SLEPT() (echo_slept[fx_now]++)
void fx_test_no_sleep(int on) { echo_no_sleep = on; }   /* (tests/sleep.c: the same playing, awake throughout) */
uint32_t fx_test_slept(int t) { return (uint32_t)t < NFX ? echo_slept[t] : 0u; }   /* (blocks it slept) */
#endif
static int fx_now;                               /* (the effect echo_asleep is asked for) */
static int echo_asleep(const f32 *sl, const f32 *sr, uint32_t line)
{
    int n;
    if (echo_quiet < line || echo_no_sleep)
        return 0;
    for (n = 0; n < CTRL; n++)
        if (sl[n] != 0.0f || sr[n] != 0.0f)
            return 0;
    ECHO_SLEPT();
    return 1;
}

/* ---- FILTER / LOFI / THUMP state ------------------------------------------------------ */
static f32 filt_l[4], filt_r[4], filt_env;
static f32 phone_s[2][2], phone_hl, phone_hr, phone_cnt;
static uint32_t phone_rng = 0x504804E1u;
static f32 punch_fast, punch_slow, punch_lp_l, punch_lp_r, punch_hi_l, punch_hi_r;

/* ---- Master EQ, Glue & Tape Trick state ----------------------------------------------- */
static f32 eq_lo[2], eq_hi[2], eq_mid[2][2];
static f32 glue_env = 0.0f;
static uint32_t tfx_w;                       /* the ring's write frame, 0..TFX_FRAMES-1 */
static int tfx_prev;
static f32 tfx_rpos, tfx_spd = 1.0f, tfx_stut_len = 5512.0f, tfx_stut_pos, tfx_stut_anchor;

static f32 SRf = 44100.0f, ISRf = 1.0f / 44100.0f;
static uint32_t fx_n;                            /* fx_send_block's calls, one a block */
static uint32_t fx_ran_at[NFX];                  /* the call an effect last ran in (fx_resume) */

/* helpers for int16 delay lines */
static H1_AI int16_t f_to_i16(f32 x)
{
    x = clampf(x, -0.9999f, 0.9999f);
    return (int16_t)(x * 32767.0f);
}
static H1_AI f32 i16_to_f(int16_t v) { return (f32)v * (1.0f / 32768.0f); }

void fx_reset(void)
{
    uint32_t i;
    for (i = 0; i < ECHO_N; i++) echo_buf[i] = 0;
    for (i = 0; i < REV_N; i++) rev_buf[i] = 0;
    for (i = 0; i < TFX_FRAMES * 2u; i++) tfx_buf[i] = 0;
    echo_quiet = 0;
    for (i = 0; i < D_NSEG; i++) d_pos[i] = 0;
    d_inlp = d_dampa = d_dampb = d_lfo1 = d_lfo2 = 0.0f;
    pl_quiet = 0;
    echo_w = 0;
    echo_t = 4000.0f;
    echo_tr = 6000.0f;
    echo_lpl = echo_lpr = echo_ol = echo_or = wow_ph = ch_ph = shf_ph = spr_lp = 0.0f;
    shf_fb_l = shf_fb_r = 0.0f;
    for (i = 0; i < 4; i++) {
        shf_i1[i] = shf_q1[i] = filt_l[i] = filt_r[i] = spr_ap[i] = 0.0f;
    }
    filt_env = 0.0f;
    phone_s[0][0] = phone_s[0][1] = phone_s[1][0] = phone_s[1][1] = 0.0f;
    phone_hl = phone_hr = phone_cnt = 0.0f;
    punch_fast = punch_slow = punch_lp_l = punch_lp_r = punch_hi_l = punch_hi_r = 0.0f;
    eq_lo[0] = eq_lo[1] = eq_hi[0] = eq_hi[1] = 0.0f;
    eq_mid[0][0] = eq_mid[0][1] = eq_mid[1][0] = eq_mid[1][1] = 0.0f;
    glue_env = 0.0f;
    tfx_prev = TRICK_OFF;
    tfx_spd = 1.0f;
    tfx_w = 0;
}

void fx_init(f32 sr)
{
    SRf = sr > 1000.0f ? sr : 44100.0f;
    ISRf = 1.0f / SRf;
    {
        int i;
        fx_n = 0;
        for (i = 0; i < NFX; i++) fx_ran_at[i] = 0;
    }
    fx_reset();
}

/* ---- Dattorro helpers ----------------------------------------------------------------- */
/* A segment's read and write, on its own position kept by the caller (in a register, not d_pos). Every delay read
 * here is shorter than its segment, so one wrap is enough. */
#define D_AT(o, len, pos, delay) rev_buf[(o) + ((pos) >= (delay) ? (pos) - (delay) : (pos) + (len) - (delay))]

static H1_AI f32 d_read_frac(uint32_t o, uint32_t len, uint32_t pos, f32 delay)
{
    int d = ifloor(delay);
    f32 fr = delay - (f32)d;
    int i0 = (int)pos - d;
    int i1;
    f32 a, b;
    while (i0 < 0) i0 += (int)len;
    while (i0 >= (int)len) i0 -= (int)len;
    i1 = i0 - 1;
    if (i1 < 0) i1 += (int)len;
    a = i16_to_f(rev_buf[o + (uint32_t)i0]);
    b = i16_to_f(rev_buf[o + (uint32_t)i1]);
    return a + (b - a) * fr;
}

/* write x at pos and step on; *nz gathers the bits written (all zero: the tank is silent) */
static H1_AI uint32_t d_put(uint32_t o, uint32_t len, uint32_t pos, f32 x, uint32_t *nz)
{
    int16_t v = f_to_i16(x);
    rev_buf[o + pos] = v;
    *nz |= (uint16_t)v;
    return pos + 1u >= len ? 0u : pos + 1u;
}

/* the allpass: its oldest sample (at pos) out, the new one in */
static H1_AI f32 d_allpass(uint32_t o, uint32_t len, uint32_t *pos, f32 x, f32 g, uint32_t *nz)
{
    f32 buf = i16_to_f(rev_buf[o + *pos]);
    f32 v = x + buf * g;
    *pos = d_put(o, len, *pos, v, nz);
    return buf - v * g;
}

/* ---- 0: PLATE (True Dattorro Modulated Stereo Plate Reverb) -------------------------- */
/* The tank's two halves meet only through their long delays: half A reads half B's last delay (segment 11) a whole
 * segment back, half B reads A's (7), and the output taps read 121 samples back or more, so within a block of CTRL
 * samples neither half reads anything the other writes in it. With the samples each reads from the other taken
 * first (pl_x7, pl_x11), the halves run apart: A and the left taps on the second core, B and the right taps on this
 * one, to the bit the same as one after the other. When input and tank have been silent for a whole longest
 * segment, every sample in the tank is 0 and stays 0: the plate sleeps until something comes in. */
static int (*fx_c2_run)(void (*fn)(uint32_t), uint32_t arg);
static int (*fx_c2_wait)(void);

void fx_two_cores(int (*run)(void (*fn)(uint32_t), uint32_t arg), int (*wait)(void))
{
    fx_c2_run = run;
    fx_c2_wait = wait;
}

static f32 pl_ap[CTRL];                      /* the input diffusers' output */
static f32 pl_x7[CTRL], pl_x11[CTRL];        /* what each half reads of the other's last delay */
static uint16_t pl_p0[D_NSEG];               /* the positions at the block's start */
static f32 pl_dec, pl_damp, pl_mod, pl_wet;
static f32 *pl_dl, *pl_dr;
static uint32_t pl_nz_a, pl_nz_b;

/* a tap on the other half's segment: q its position after this sample's write */
#define PL_TAP(seg, len, q, delay) i16_to_f(D_AT(D_O##seg, len, q, delay))
#define PL_STEP(q, len) ((q) = (q) + 1u >= (len) ? 0u : (q) + 1u)

H1_HOT static void plate_half_a(uint32_t arg)
{
    uint32_t p4 = pl_p0[4], p5 = pl_p0[5], p6 = pl_p0[6], p7 = pl_p0[7], nz = 0;
    uint32_t q9 = pl_p0[9], q10 = pl_p0[10], q11 = pl_p0[11];
    f32 dec = pl_dec, damp = pl_damp, mod = pl_mod, wet = pl_wet, lfo = d_lfo1, damps = d_dampa;
    int n;
    (void)arg;
    for (n = 0; n < CTRL; n++) {
        f32 ta, ma, va, ol;
        lfo += 0.67f * ISRf; if (lfo >= 1.0f) lfo -= 1.0f;
        ta = pl_ap[n] + pl_x11[n] * dec;
        ma = d_read_frac(D_O4, D_AP1A, p4, 672.0f + mod * tsin(lfo));
        va = softclip(ta + ma * 0.68f);
        p4 = d_put(D_O4, D_AP1A, p4, va, &nz);
        ta = ma - va * 0.68f;
        p5 = d_put(D_O5, D_DL1A, p5, ta, &nz);
        ta = i16_to_f(D_AT(D_O5, D_DL1A, p5, D_DL1A - 1));
        damps += (ta - damps) * (1.0f - damp);
        ta = d_allpass(D_O6, D_AP2A, &p6, damps * dec, -0.50f, &nz);
        p7 = d_put(D_O7, D_DL2A, p7, softclip(ta), &nz);
        /* (B's segments where B leaves them at this sample: the taps read none it writes in this block) */
        PL_STEP(q9, D_DL1B); PL_STEP(q10, D_AP2B); PL_STEP(q11, D_DL2B);
        ol = 0.6f * (PL_TAP(9, D_DL1B, q9, 266) + PL_TAP(9, D_DL1B, q9, 2974) - PL_TAP(10, D_AP2B, q10, 1913)
                   + PL_TAP(11, D_DL2B, q11, 1996) - i16_to_f(D_AT(D_O5, D_DL1A, p5, 1990))
                   - i16_to_f(D_AT(D_O6, D_AP2A, p6, 187)) - i16_to_f(D_AT(D_O7, D_DL2A, p7, 1066)));
        pl_dl[n] += ol * wet * 0.55f;
    }
    d_pos[4] = (uint16_t)p4; d_pos[5] = (uint16_t)p5; d_pos[6] = (uint16_t)p6; d_pos[7] = (uint16_t)p7;
    d_lfo1 = lfo;
    d_dampa = damps;
    pl_nz_a = nz;
}

H1_HOT static void plate_half_b(void)
{
    uint32_t p8 = pl_p0[8], p9 = pl_p0[9], p10 = pl_p0[10], p11 = pl_p0[11], nz = 0;
    uint32_t q5 = pl_p0[5], q6 = pl_p0[6], q7 = pl_p0[7];
    f32 dec = pl_dec, damp = pl_damp, mod = pl_mod, wet = pl_wet, lfo = d_lfo2, damps = d_dampb;
    int n;
    for (n = 0; n < CTRL; n++) {
        f32 tb, mb, vb, or_;
        lfo += 0.91f * ISRf; if (lfo >= 1.0f) lfo -= 1.0f;
        tb = pl_ap[n] + pl_x7[n] * dec;
        mb = d_read_frac(D_O8, D_AP1B, p8, 908.0f + mod * tsin(lfo));
        vb = softclip(tb + mb * 0.68f);
        p8 = d_put(D_O8, D_AP1B, p8, vb, &nz);
        tb = mb - vb * 0.68f;
        p9 = d_put(D_O9, D_DL1B, p9, tb, &nz);
        tb = i16_to_f(D_AT(D_O9, D_DL1B, p9, D_DL1B - 1));
        damps += (tb - damps) * (1.0f - damp);
        tb = d_allpass(D_O10, D_AP2B, &p10, damps * dec, -0.50f, &nz);
        p11 = d_put(D_O11, D_DL2B, p11, softclip(tb), &nz);
        PL_STEP(q5, D_DL1A); PL_STEP(q6, D_AP2A); PL_STEP(q7, D_DL2A);
        or_ = 0.6f * (PL_TAP(5, D_DL1A, q5, 353) + PL_TAP(5, D_DL1A, q5, 3627) - PL_TAP(6, D_AP2A, q6, 1228)
                    + PL_TAP(7, D_DL2A, q7, 2673) - i16_to_f(D_AT(D_O9, D_DL1B, p9, 2111))
                    - i16_to_f(D_AT(D_O10, D_AP2B, p10, 335)) - i16_to_f(D_AT(D_O11, D_DL2B, p11, 121)));
        pl_dr[n] += or_ * wet * 0.55f;
    }
    d_pos[8] = (uint16_t)p8; d_pos[9] = (uint16_t)p9; d_pos[10] = (uint16_t)p10; d_pos[11] = (uint16_t)p11;
    d_lfo2 = lfo;
    d_dampb = damps;
    pl_nz_b = nz;
}

static void fx_plate(const f32 *sl, const f32 *sr, f32 *dl, f32 *dr, const uint8_t p[4])
{
    f32 mix = (f32)p[0] * 0.01f;
    f32 bw = 0.65f, inlp = d_inlp;
    uint32_t p0 = d_pos[0], p1 = d_pos[1], p2 = d_pos[2], p3 = d_pos[3], nz = 0, x7, x11, heard = 0;
    int n;

    if (mix <= 0.001f)
        return;
    for (n = 0; n < CTRL; n++)
        heard |= sl[n] != 0.0f || sr[n] != 0.0f;
    if (!heard && pl_quiet >= D_DL1A + CTRL) {
        for (n = 0; n < CTRL; n++) {         /* asleep: the tank is all 0 and nothing comes in */
            d_lfo1 += 0.67f * ISRf; if (d_lfo1 >= 1.0f) d_lfo1 -= 1.0f;
            d_lfo2 += 0.91f * ISRf; if (d_lfo2 >= 1.0f) d_lfo2 -= 1.0f;
        }
        return;
    }
    pl_dec = 0.35f + 0.59f * ((f32)p[1] * 0.01f);
    pl_damp = 0.08f + 0.72f * ((f32)p[2] * 0.01f);
    pl_mod = (f32)p[3] * 0.01f * 10.0f;
    pl_wet = mix * (0.45f + 0.75f * mix);
    pl_dl = dl;
    pl_dr = dr;

    /* input bandwidth filter + 4 cascaded input diffusers */
    for (n = 0; n < CTRL; n++) {
        f32 in = (sl[n] + sr[n]) * 0.35f + 1e-18f, ap;
        inlp += (in - inlp) * bw;
        ap = d_allpass(D_O0, D_IN1, &p0, inlp, 0.72f, &nz);
        ap = d_allpass(D_O1, D_IN2, &p1, ap, 0.72f, &nz);
        ap = d_allpass(D_O2, D_IN3, &p2, ap, 0.60f, &nz);
        pl_ap[n] = d_allpass(D_O3, D_IN4, &p3, ap, 0.60f, &nz);
    }
    d_inlp = inlp;
    d_pos[0] = (uint16_t)p0; d_pos[1] = (uint16_t)p1; d_pos[2] = (uint16_t)p2; d_pos[3] = (uint16_t)p3;

    /* what each half reads of the other: a whole segment back, the oldest sample, before it is written over */
    for (n = 0; n < D_NSEG; n++)
        pl_p0[n] = d_pos[n];
    x7 = pl_p0[7];
    x11 = pl_p0[11];
    for (n = 0; n < CTRL; n++) {
        x7 = x7 + 1u >= D_DL2A ? 0u : x7 + 1u;
        x11 = x11 + 1u >= D_DL2B ? 0u : x11 + 1u;
        pl_x7[n] = i16_to_f(rev_buf[D_O7 + x7]);
        pl_x11[n] = i16_to_f(rev_buf[D_O11 + x11]);
    }

    if (fx_c2_run && fx_c2_run(H1_FAR(plate_half_a), 0)) {   /* (both halves run from RAM: H1_HOT) */
        H1_FAR(plate_half_b)();
        if (fx_c2_wait() != 0)
            H1_FAR(plate_half_a)(0);         /* (the second core gave up: the job was dropped) */
    } else {
        H1_FAR(plate_half_a)(0);
        H1_FAR(plate_half_b)();
    }
    if (heard || nz || pl_nz_a || pl_nz_b)
        pl_quiet = 0;
    else if (pl_quiet < D_DL1A + CTRL)
        pl_quiet += CTRL;
}

/* ---- echo_buf integer-indexed fractional reader --------------------------------------- */
static inline f32 echo_read_frac(uint32_t mask, uint32_t base, uint32_t w, f32 delay)
{
    int32_t d = ifloor(delay);
    f32 fr = 1.0f - (delay - (f32)d), a, b;
    uint32_t i = w - (uint32_t)d - 1u;
    a = i16_to_f(echo_buf[base + (i & mask)]);
    b = i16_to_f(echo_buf[base + ((i + 1u) & mask)]);
    return a + (b - a) * fr;
}

/* ---- 1: DELAY (Modulated Stereo Tape / BBD Ping-Pong Echo) ---------------------------- */
static void fx_delay(const f32 *sl, const f32 *sr, f32 *dl, f32 *dr, const uint8_t p[4], f32 bpm)
{
    static const f32 DIV[8] = {0.125f, 0.25f, 1.0f / 3.0f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
    f32 mix = (f32)p[0] * 0.01f;
    f32 fb = 0.08f + 0.78f * ((f32)p[2] * 0.01f);
    f32 tape = (f32)p[3] * 0.01f;
    f32 tgt = DIV[p[1] & 7] * 60.0f / maxf(40.0f, bpm) * SRf * 0.5f;   /* half-rate -> up to 1.48 s */
    f32 lp_k = 0.85f - 0.62f * tape;
    int n;

    f32 tgt_r;
    while (tgt > 15200.0f) tgt *= 0.5f;
    if (tgt < 24.0f) tgt = 24.0f;
    /* the right side half as long again, or, where that would run past its 16,384 samples, three quarters
     * (it wrapped to a few ms: 120 BPM 1/4 gave a 7 ms echo on the right) */
    tgt_r = tgt * 1.5f + 40.0f < 16383.0f ? tgt * 1.5f : tgt * 0.75f;
    if (mix <= 0.001f)
        return;
    if (echo_lpl == 0.0f && echo_lpr == 0.0f && echo_ol == 0.0f && echo_or == 0.0f && echo_asleep(sl, sr, 16384u)) {
        for (n = 0; n < CTRL; n += 2) {          /* asleep (echo_quiet) */
            wow_ph += 0.62f * 2.0f * ISRf; if (wow_ph >= 1.0f) wow_ph -= 1.0f;
            echo_t += (tgt - echo_t) * 0.0018f;
            echo_tr += (tgt_r - echo_tr) * 0.0018f;
            echo_w++;
        }
        return;
    }

    for (n = 0; n < CTRL; n += 2) {
        f32 inl = (sl[n] + sl[n + 1]) * 0.5f * mix;
        f32 inr = (sr[n] + sr[n + 1]) * 0.5f * mix;
        f32 wow, tl, tr;
        wow_ph += 0.62f * 2.0f * ISRf; if (wow_ph >= 1.0f) wow_ph -= 1.0f;
        echo_t += (tgt - echo_t) * 0.0018f;
        echo_tr += (tgt_r - echo_tr) * 0.0018f;
        wow = tape * tape * 18.0f * tsin(wow_ph);
        tl = echo_read_frac(16383u, 0u, echo_w, maxf(8.0f, echo_t + wow));
        tr = echo_read_frac(16383u, 16384u, echo_w, clampf(echo_tr - wow, 8.0f, 16360.0f));

        /* ping-pong cross-feedback with warm tape damping & saturation */
        echo_lpl += (tr * fb - echo_lpl) * lp_k;
        echo_lpr += (tl * fb - echo_lpr) * lp_k;
        echo_buf[echo_w & 16383u] = f_to_i16(softclip((inl + echo_lpl) * (1.0f + 0.5f * tape)));
        echo_buf[16384u + (echo_w & 16383u)] = f_to_i16(softclip((inr + echo_lpr) * (1.0f + 0.5f * tape)));
        echo_wrote(echo_buf[echo_w & 16383u], echo_buf[16384u + (echo_w & 16383u)]);
        echo_w++;

        dl[n]     += (echo_ol + tl) * 0.5f;
        dr[n]     += (echo_or + tr) * 0.5f;
        dl[n + 1] += tl;
        dr[n + 1] += tr;
        echo_ol = tl;
        echo_or = tr;
    }
}

/* ---- 2: FILTER (Dual Resonant ZDF Filter + Envelope Follower) --------------------- */
static void fx_filter(f32 *dl, f32 *dr, const uint8_t p[4])
{
    f32 lpf_base = 80.0f * exp2fx((f32)p[0] * 0.01f * 7.8f);
    f32 hpf_hz = 20.0f * exp2fx((f32)p[1] * 0.01f * 7.2f);
    f32 q = 0.707f + 7.5f * ((f32)p[2] * 0.01f) * ((f32)p[2] * 0.01f);
    f32 env_amt = ((f32)p[3] - 50.0f) * 0.02f;   /* bipolar: <50 closes on hits, >50 opens */
    Svf flp, fhp;
    f32 peak = 0.0f, fc;
    int n;

    for (n = 0; n < CTRL; n++) {
        f32 m = maxf(absf(dl[n]), absf(dr[n]));
        if (m > peak) peak = m;
    }
    filt_env += (peak - filt_env) * (peak > filt_env ? 0.35f : 0.03f);
    fc = clampf(lpf_base * exp2fx(env_amt * filt_env * 6.0f), 40.0f, 18000.0f);
    svf_set(&flp, fc, q, ISRf);
    svf_set(&fhp, clampf(hpf_hz, 20.0f, 12000.0f), 0.707f + 0.4f * (q - 0.707f), ISRf);

    for (n = 0; n < CTRL; n++) {
        f32 l1, b1, l2, b2, xl = dl[n], xr = dr[n];
        SVF_TICK(flp, &filt_l[0], xl, l1, b1);
        SVF_TICK(fhp, &filt_l[2], l1, l2, b2);
        dl[n] = p[1] > 1 ? (l1 - l2 - fhp.k * b2) : l1;

        SVF_TICK(flp, &filt_r[0], xr, l1, b1);
        SVF_TICK(fhp, &filt_r[2], l1, l2, b2);
        dr[n] = p[1] > 1 ? (l1 - l2 - fhp.k * b2) : l1;
    }
}

/* ---- 3: SHIFT (Frequency Shifter + Resonant Comb Delay) ---------------------------- */
/* 4-pole allpass Hilbert quadrature pair (Olli Niemitalo coefficients, ~0.7 deg accuracy) */
static const f32 HILB_A[4] = {0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f};
static const f32 HILB_B[4] = {0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f};

static void fx_shift(const f32 *sl, const f32 *sr, f32 *dl, f32 *dr, const uint8_t p[4])
{
    f32 fnorm = ((f32)p[0] - 50.0f) * 0.02f;                      /* -1..+1 */
    f32 shift_hz = fnorm * fnorm * (fnorm < 0.0f ? -320.0f : 320.0f);
    f32 dly_s = 0.002f * exp2fx((f32)p[1] * 0.01f * 6.5f);        /* 2 ms .. 180 ms */
    f32 dly_samp = clampf(dly_s * SRf, 16.0f, 15000.0f);
    f32 fb = (f32)p[2] * 0.01f * 0.84f;
    f32 mix = (f32)p[3] * 0.01f;
    f32 dph = shift_hz * ISRf;
    int n, j, quiet = shf_fb_l == 0.0f && shf_fb_r == 0.0f;

    if (mix <= 0.001f)
        return;
    for (j = 0; j < 4; j++)
        quiet &= shf_i1[j] == 0.0f && shf_q1[j] == 0.0f;
    if (quiet && echo_asleep(sl, sr, 16384u)) {
        for (n = 0; n < CTRL; n++) {             /* asleep (echo_quiet) */
            shf_ph += dph;
            if (shf_ph >= 1.0f) shf_ph -= 1.0f;
            if (shf_ph < 0.0f) shf_ph += 1.0f;
            echo_w++;
        }
        return;
    }

    for (n = 0; n < CTRL; n++) {
        f32 in = (sl[n] + sr[n]) * 0.5f + 0.5f * (shf_fb_l + shf_fb_r) * fb;
        f32 x1 = in, x2 = in, cs, sn, up, dn, yl, yr;
        for (j = 0; j < 4; j++) {
            f32 t = x1 - HILB_A[j] * shf_i1[j];
            x1 = shf_i1[j] + HILB_A[j] * t;
            shf_i1[j] = t;

            t = x2 - HILB_B[j] * shf_q1[j];
            x2 = shf_q1[j] + HILB_B[j] * t;
            shf_q1[j] = t;
        }
        shf_ph += dph;
        if (shf_ph >= 1.0f) shf_ph -= 1.0f;
        if (shf_ph < 0.0f) shf_ph += 1.0f;
        cs = tcos(shf_ph);
        sn = tsin(shf_ph);
        /* stereo frequency shift: L gets upper sideband, R gets slight opposite/quadrature! */
        up = x1 * cs - x2 * sn;
        dn = x1 * cs + x2 * sn * 0.85f;

        echo_buf[echo_w & 16383u] = f_to_i16(softclip(up));
        echo_buf[16384u + (echo_w & 16383u)] = f_to_i16(softclip(dn));
        echo_wrote(echo_buf[echo_w & 16383u], echo_buf[16384u + (echo_w & 16383u)]);
        yl = echo_read_frac(16383u, 0u, echo_w, dly_samp);
        yr = echo_read_frac(16383u, 16384u, echo_w, dly_samp * 1.03f);
        echo_w++;

        shf_fb_l = yl;
        shf_fb_r = yr;
        dl[n] += yl * mix;
        dr[n] += yr * mix;
    }
}

/* ---- 4: CHORUS (Juno-style 3-Tap Stereo Ensemble) ------------------------------- */
static void fx_chorus(const f32 *sl, const f32 *sr, f32 *dl, f32 *dr, const uint8_t p[4])
{
    f32 mix = (f32)p[0] * 0.01f;
    f32 rate = 0.15f * exp2fx((f32)p[1] * 0.01f * 5.2f);          /* 0.15 .. 5.5 Hz */
    f32 depth = (0.0005f + 0.0055f * ((f32)p[2] * 0.01f)) * SRf;  /* 0.5 .. 6 ms */
    f32 width = (f32)p[3] * 0.01f;
    f32 base_d = 0.008f * SRf;
    int n;

    if (mix <= 0.001f)
        return;
    if (echo_asleep(sl, sr, 4096u)) {
        for (n = 0; n < CTRL; n++) {             /* asleep (echo_quiet) */
            ch_ph += rate * ISRf;
            if (ch_ph >= 1.0f) ch_ph -= 1.0f;
            echo_w++;
        }
        return;
    }

    for (n = 0; n < CTRL; n++) {
        f32 t1, t2, t3, wl, wr;
        echo_buf[echo_w & 4095u] = f_to_i16(sl[n]);
        echo_buf[4096u + (echo_w & 4095u)] = f_to_i16(sr[n]);
        echo_wrote(echo_buf[echo_w & 4095u], echo_buf[4096u + (echo_w & 4095u)]);

        ch_ph += rate * ISRf;
        if (ch_ph >= 1.0f) ch_ph -= 1.0f;

        t1 = echo_read_frac(4095u, 0u, echo_w, base_d + depth * tsin(ch_ph));
        t2 = echo_read_frac(4095u, 4096u, echo_w, base_d * 1.17f + depth * tsin(ch_ph + 0.3333f));
        t3 = echo_read_frac(4095u, 0u, echo_w, base_d * 1.37f + depth * tsin(ch_ph + 0.6667f));
        echo_w++;

        wl = (t1 + 0.55f * t3) * 0.65f;
        wr = (t2 + 0.55f * t3) * 0.65f;
        dl[n] += lerpf((wl + wr) * 0.5f, wl, width) * mix;
        dr[n] += lerpf((wl + wr) * 0.5f, wr, width) * mix;
    }
}

/* ---- 5: TANK (Dispersive Spring Tank Reverb) ---------------------------------------- */
static void fx_tank(const f32 *sl, const f32 *sr, f32 *dl, f32 *dr, const uint8_t p[4])
{
    f32 mix = (f32)p[0] * 0.01f;
    f32 dec = 0.45f + 0.48f * ((f32)p[1] * 0.01f);
    f32 twang = 0.35f + 0.42f * ((f32)p[2] * 0.01f);
    f32 tone = 0.25f + 0.65f * ((f32)p[3] * 0.01f);
    int n;

    if (mix <= 0.001f)
        return;
    if (spr_lp == 0.0f && spr_ap[0] == 0.0f && spr_ap[1] == 0.0f && spr_ap[2] == 0.0f && spr_ap[3] == 0.0f
        && echo_asleep(sl, sr, 8192u)) {
        echo_w += CTRL;                          /* asleep (echo_quiet) */
        return;
    }

    for (n = 0; n < CTRL; n++) {
        f32 in = (sl[n] + sr[n]) * 0.45f;
        f32 d1 = echo_read_frac(8191u, 0u, echo_w, 2411.0f);
        f32 d2 = echo_read_frac(8191u, 8192u, echo_w, 2963.0f);
        f32 x = in + (d1 * 0.65f + d2 * 0.35f) * dec;
        int j;
        /* 4 short allpass dispersion stages -> metallic spring chirp! */
        for (j = 0; j < 4; j++) {
            f32 v = x - twang * spr_ap[j];
            x = spr_ap[j] + twang * v;
            spr_ap[j] = v;
        }
        spr_lp += (x - spr_lp) * tone;
        echo_buf[echo_w & 8191u] = f_to_i16(softclip(spr_lp));
        echo_buf[8192u + (echo_w & 8191u)] = f_to_i16(softclip(in * 0.8f + d1 * dec * 0.9f - d2 * 0.25f));
        echo_wrote(echo_buf[echo_w & 8191u], echo_buf[8192u + (echo_w & 8191u)]);
        echo_w++;

        dl[n] += (d1 * 0.8f + d2 * 0.3f) * mix;
        dr[n] += (d2 * 0.8f + d1 * 0.3f) * mix;
    }
}

/* ---- 6: LOFI (Lo-Fi Speaker + Bitcrush + Rate Crush + Grit) --------------------- */
static void fx_lofi(f32 *dl, f32 *dr, const uint8_t p[4])
{
    f32 crush = (f32)p[0] * 0.01f;
    f32 rate = (f32)p[1] * 0.01f;
    f32 band = (f32)p[2] * 0.01f;
    f32 grit = (f32)p[3] * 0.01f;
    f32 steps = 256.0f * exp2fx(-crush * 6.2f);        /* 256 down to ~3.5 quantization levels */
    f32 hstep = 1.0f - rate * 0.92f;
    f32 fc = 500.0f * exp2fx(band * 3.0f);             /* 500 .. 4000 Hz */
    Svf bp;
    int n;

    svf_set(&bp, fc, 1.1f + 1.4f * band, ISRf);

    for (n = 0; n < CTRL; n++) {
        f32 xl = dl[n], xr = dr[n], lo, b;
        phone_cnt += hstep;
        if (phone_cnt >= 1.0f) {
            phone_cnt -= 1.0f;
            if (crush > 0.02f) {
                phone_hl = (f32)ifloor(xl * steps + 0.5f) / steps;
                phone_hr = (f32)ifloor(xr * steps + 0.5f) / steps;
            } else {
                phone_hl = xl;
                phone_hr = xr;
            }
        }
        xl = phone_hl + grit * grit * 0.05f * noise_s(&phone_rng);
        xr = phone_hr + grit * grit * 0.05f * noise_s(&phone_rng);
        if (band > 0.02f) {
            SVF_TICK(bp, phone_s[0], xl, lo, b); (void)lo;
            xl = lerpf(xl, softclip(b * 2.1f) * 0.85f, sat(band * 1.4f));
            SVF_TICK(bp, phone_s[1], xr, lo, b); (void)lo;
            xr = lerpf(xr, softclip(b * 2.1f) * 0.85f, sat(band * 1.4f));
        }
        dl[n] = softclip(xl * (1.0f + 2.5f * grit)) / (1.0f + 0.7f * grit);
        dr[n] = softclip(xr * (1.0f + 2.5f * grit)) / (1.0f + 0.7f * grit);
    }
}

/* ---- 7: THUMP (Transient Shaper + Tape Drive + Bass Boost) ----------------------- */
static void fx_thump(f32 *dl, f32 *dr, const uint8_t p[4])
{
    f32 punch = (f32)p[0] * 0.01f;
    f32 drive = 1.0f + 3.5f * ((f32)p[1] * 0.01f) * ((f32)p[1] * 0.01f);
    f32 comp = 1.0f / (1.0f + 0.55f * (drive - 1.0f));
    f32 bass = (f32)p[2] * 0.01f * 0.85f;
    f32 tone = ((f32)p[3] - 50.0f) * 0.02f;
    f32 klp = onepole_k(1.0f / (H1_TAU * 110.0f), SRf);
    f32 khi = onepole_k(1.0f / (H1_TAU * 3600.0f), SRf);
    int n;

    for (n = 0; n < CTRL; n++) {
        f32 m = maxf(absf(dl[n]), absf(dr[n]));
        f32 tr, g, xl, xr;
        punch_fast += (m - punch_fast) * (m > punch_fast ? 0.12f : 0.008f);
        punch_slow += (m - punch_slow) * 0.0012f;
        tr = maxf(0.0f, punch_fast - punch_slow);
        g = 1.0f + punch * minf(2.0f, tr * 4.5f);

        xl = dl[n] * g;
        xr = dr[n] * g;
        punch_lp_l += (xl - punch_lp_l) * klp;
        punch_lp_r += (xr - punch_lp_r) * klp;
        xl += punch_lp_l * bass;
        xr += punch_lp_r * bass;

        punch_hi_l += (xl - punch_hi_l) * khi;
        punch_hi_r += (xr - punch_hi_r) * khi;
        xl += (xl - punch_hi_l) * tone * 0.65f;
        xr += (xr - punch_hi_r) * tone * 0.65f;

        dl[n] = softclip(xl * drive) * comp;
        dr[n] = softclip(xr * drive) * comp;
    }
}

/* An effect that did not run in the block before (another was chosen, FX was bypassed, or its MIX was at 0) starts
 * from silence. Its state was frozen while it did not run, so it came back with whatever was in its buffers: PLATE
 * after six silent seconds on FILTER still gave -26 dBFS of the old tail. */
static void fx_resume(int t)
{
    uint32_t i;
    switch (t) {
    case FX_PLATE:
        for (i = 0; i < REV_N; i++) rev_buf[i] = 0;
        d_inlp = d_dampa = d_dampb = 0.0f;
        pl_quiet = D_DL1A + CTRL;                /* (all 0: it starts asleep) */
        break;
    case FX_DELAY: case FX_SHIFT: case FX_CHORUS: case FX_TANK:
        for (i = 0; i < ECHO_N; i++) echo_buf[i] = 0;
        echo_lpl = echo_lpr = echo_ol = echo_or = spr_lp = shf_fb_l = shf_fb_r = 0.0f;
        for (i = 0; i < 4; i++) shf_i1[i] = shf_q1[i] = spr_ap[i] = 0.0f;
        echo_quiet = ECHO_QUIET_MAX;             /* (all 0: it starts asleep) */
        break;
    case FX_FILTER:
        for (i = 0; i < 4; i++) filt_l[i] = filt_r[i] = 0.0f;
        filt_env = 0.0f;
        break;
    case FX_LOFI:
        phone_s[0][0] = phone_s[0][1] = phone_s[1][0] = phone_s[1][1] = 0.0f;
        phone_hl = phone_hr = phone_cnt = 0.0f;
        break;
    default:
        punch_fast = punch_slow = punch_lp_l = punch_lp_r = punch_hi_l = punch_hi_r = 0.0f;
        break;
    }
}

void fx_send_block(const f32 *sl, const f32 *sr, f32 *dl, f32 *dr, int fx_type, const uint8_t p[4], f32 bpm, int fx_on)
{
    fx_n++;
    if (!fx_on)
        return;
    fx_type = iclamp(fx_type, 0, NFX - 1);
    if (fx_type != FX_FILTER && fx_type != FX_LOFI && fx_type != FX_THUMP && p[fx_type == FX_SHIFT ? 3 : 0] == 0)
        return;                                  /* (MIX at 0: it does not run) */
    if (fx_ran_at[fx_type] != fx_n - 1u)
        fx_resume(fx_type);
    fx_ran_at[fx_type] = fx_n;
    fx_now = fx_type;
    switch (fx_type) {
    case FX_PLATE: fx_plate(sl, sr, dl, dr, p); break;
    case FX_DELAY:  fx_delay(sl, sr, dl, dr, p, bpm); break;
    case FX_FILTER:  fx_filter(dl, dr, p); break;
    case FX_SHIFT:    fx_shift(sl, sr, dl, dr, p); break;
    case FX_CHORUS: fx_chorus(sl, sr, dl, dr, p); break;
    case FX_TANK: fx_tank(sl, sr, dl, dr, p); break;
    case FX_LOFI:  fx_lofi(dl, dr, p); break;
    case FX_THUMP:  fx_thump(dl, dr, p); break;
    default: break;
    }
}

/* ---- Master Bus: 3-Band Master EQ + Tape Drive/Glue + RAM Tape Performance FX ---- */
/* the master bus's EQ and drive, every sample of every block (fx_master_block): from RAM */
H1_HOT static void master_eq(f32 *l, f32 *r, const Svf *fm, int eq_on, f32 klo, f32 khi, f32 lo_g, f32 md_g,
                             f32 hi_g, f32 drv, f32 drive, f32 comp_g)
{
    Svf fmid;
    int n;
    fmid.g = fm->g; fmid.k = fm->k; fmid.a1 = fm->a1; fmid.a2 = fm->a2; fmid.a3 = fm->a3;   /* (no struct copy: memcpy) */
    for (n = 0; n < CTRL; n++) {
        f32 xl = l[n], xr = r[n];
        /* 3-band EQ */
        if (eq_on) {
            f32 lp, bp;
            eq_lo[0] += (xl - eq_lo[0]) * klo;
            eq_lo[1] += (xr - eq_lo[1]) * klo;
            eq_hi[0] += (xl - eq_hi[0]) * khi;
            eq_hi[1] += (xr - eq_hi[1]) * khi;
            SVF_TICK(fmid, eq_mid[0], xl, lp, bp); (void)lp;
            xl += eq_lo[0] * lo_g * 0.9f + bp * md_g * 0.75f + (xl - eq_hi[0]) * hi_g * 0.85f;
            SVF_TICK(fmid, eq_mid[1], xr, lp, bp); (void)lp;
            xr += eq_lo[1] * lo_g * 0.9f + bp * md_g * 0.75f + (xr - eq_hi[1]) * hi_g * 0.85f;
        }
        /* Master Tape Drive + subtle soft-knee glue compression */
        if (drv > 0.01f) {
            f32 pk = maxf(absf(xl), absf(xr));
            f32 gr;
            glue_env += (pk - glue_env) * (pk > glue_env ? 0.03f : 0.0005f);
            gr = glue_env > 0.35f ? 1.0f / (1.0f + (glue_env - 0.35f) * (1.2f + 1.8f * drv)) : 1.0f;
            xl = softclip(xl * gr * drive) * comp_g * (1.0f + 0.25f * drv);
            xr = softclip(xr * gr * drive) * comp_g * (1.0f + 0.25f * drv);
        }
        l[n] = xl;
        r[n] = xr;
    }
}

void fx_master_block(f32 *l, f32 *r, const uint8_t master[4], int trick, f32 bpm)
{
    f32 lo_g = ((f32)master[0] - 50.0f) * 0.02f;   /* -1..+1 -> ~-6..+8 dB */
    f32 md_g = ((f32)master[1] - 50.0f) * 0.02f;
    f32 hi_g = ((f32)master[2] - 50.0f) * 0.02f;
    f32 drv  = (f32)master[3] * 0.01f;
    f32 klo = onepole_k(1.0f / (H1_TAU * 130.0f), SRf);
    f32 khi = onepole_k(1.0f / (H1_TAU * 4800.0f), SRf);
    f32 drive = 1.0f + 2.8f * drv * drv;
    f32 comp_g = 1.0f / (1.0f + 0.55f * (drive - 1.0f));
    Svf fmid;
    int eq_on = (master[0] != 50 || master[1] != 50 || master[2] != 50);
    int n;

    if (eq_on)
        svf_set(&fmid, 1100.0f, 0.9f, ISRf);
    else
        fmid.g = fmid.k = fmid.a1 = fmid.a2 = fmid.a3 = 0.0f;   /* (not read: the EQ is off) */
    if (eq_on || drv > 0.01f)
        H1_FAR(master_eq)(l, r, &fmid, eq_on, klo, khi, lo_g, md_g, hi_g, drv, drive, comp_g);

    /* Live RAM Tape Performance FX: Brake & Beat Stutter (48 KB stereo ring) */
    if (trick != tfx_prev) {
        if (trick == TRICK_BRAKE) {
            tfx_rpos = 0.0f;
            tfx_spd = 1.0f;
        } else if (trick >= TRICK_STUT8 && trick <= TRICK_STUT32) {
            static const f32 SDIV[3] = {0.5f, 0.25f, 0.125f};
            f32 len = SDIV[trick - TRICK_STUT8] * 60.0f / maxf(40.0f, bpm) * SRf;
            while (len > (f32)(TFX_FRAMES - 64u)) len *= 0.5f;   /* (in time: clamped, 1/8 was 277 ms below 108 BPM) */
            tfx_stut_len = maxf(len, 256.0f);
            tfx_stut_anchor = (f32)((tfx_w % TFX_FRAMES + TFX_FRAMES - (uint32_t)tfx_stut_len) % TFX_FRAMES);
            tfx_stut_pos = 0.0f;
        }
        tfx_prev = trick;
    }

    for (n = 0; n < CTRL; n++) {
        uint32_t wi = tfx_w;                     /* (kept below TFX_FRAMES: no division a sample) */
        if (trick < TRICK_STUT8) {
            tfx_buf[wi * 2u]     = f_to_i16(l[n]);
            tfx_buf[wi * 2u + 1] = f_to_i16(r[n]);
            tfx_w = wi + 1u >= TFX_FRAMES ? 0u : wi + 1u;
        }
        if (trick == TRICK_BRAKE) {
            uint32_t back, i0, i1, curw = tfx_w % TFX_FRAMES;
            f32 fr, al, ar, bl, br;
            tfx_spd = maxf(0.0f, tfx_spd - 2.3f * ISRf);
            tfx_rpos += (1.0f - tfx_spd);
            if (tfx_rpos > (f32)(TFX_FRAMES - 4u))
                tfx_rpos = (f32)(TFX_FRAMES - 4u);
            back = (uint32_t)ifloor(tfx_rpos);
            fr = tfx_rpos - (f32)back;
            i0 = (curw + TFX_FRAMES - 1u - (back % TFX_FRAMES)) % TFX_FRAMES;
            i1 = (i0 + TFX_FRAMES - 1u) % TFX_FRAMES;
            al = i16_to_f(tfx_buf[i0 * 2u]);     ar = i16_to_f(tfx_buf[i0 * 2u + 1]);
            bl = i16_to_f(tfx_buf[i1 * 2u]);     br = i16_to_f(tfx_buf[i1 * 2u + 1]);
            l[n] = (al + (bl - al) * fr) * sat(tfx_spd * 6.0f);
            r[n] = (ar + (br - ar) * fr) * sat(tfx_spd * 6.0f);
        } else if (trick >= TRICK_STUT8 && trick <= TRICK_STUT32) {
            uint32_t idx = ((uint32_t)(tfx_stut_anchor + tfx_stut_pos)) % TFX_FRAMES;
            f32 win = 1.0f;
            if (tfx_stut_pos < 64.0f) win = tfx_stut_pos * (1.0f / 64.0f);
            else if (tfx_stut_pos > tfx_stut_len - 64.0f) win = (tfx_stut_len - tfx_stut_pos) * (1.0f / 64.0f);
            l[n] = i16_to_f(tfx_buf[idx * 2u]) * win;
            r[n] = i16_to_f(tfx_buf[idx * 2u + 1]) * win;
            tfx_stut_pos += 1.0f;
            if (tfx_stut_pos >= tfx_stut_len)
                tfx_stut_pos -= tfx_stut_len;
        }
    }
}
