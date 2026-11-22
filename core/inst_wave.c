/* A22 - WAVE: a wavetable synthesizer.
 *
 * Each algorithm is a bank of fifteen single-cycle waves (core/wave_data.c, made by tools/gen_wavetables.py):
 * eight main ones and seven between them; K1 morphs through them. Every wave is kept at six levels an octave
 * apart, each with half the harmonics of the one before, and a note reads the level whose highest harmonic
 * stays under ~18 kHz, so nothing folds back above the Nyquist frequency however high it plays. Two
 * oscillators, left and right, can be detuned and read a little apart in the bank for width, then a
 * resonant low-pass per side.
 *
 *   K1 (BLUE)   WAVE    place in the bank, morphing smoothly from wave to wave
 *   K2 (GREEN)  SCAN    how far the place moves up the bank as the mod envelope dies away (evolving notes)
 *   K3 (WHITE)  SPREAD  stereo: at 0 both sides the same (centred). Turned up, the right side is blended
 *                       in from a second oscillator that starts further into its wave, is detuned (up to
 *                       ~15 cents) and reads further along the bank, so the width grows over the whole turn
 *   K4 (ORANGE) FILTER  low-pass cutoff (Mod-Env and velocity open it) */
#include "internal.h"
#include "dsp.h"
#include "wave_data.h"

static struct {
    int algo;
    f32 pos, scan, spread, det, cut, q;
} W;

static void wave_block(const Blk *b)
{
    W.algo   = b->algo & 7;
    W.pos    = b->k[0] * (f32)(WT_WAVES - 1);
    W.scan   = b->k[1];
    W.spread = b->k[2];
    W.det    = 0.009f * W.spread * W.spread;                  /* the right side's detune: up to ~15 cents */
    W.cut    = 140.0f * exp2fx(b->k[3] * 7.1f);              /* 140 Hz .. 19 kHz */
    W.q      = 0.62f + 0.9f * (1.0f - b->k[3]) * (1.0f - b->k[3]);
}

/* the place in the bank this voice reads now: the knob, moved up by SCAN as the mod envelope falls */
static f32 wave_place(const Voice *v, const Blk *b)
{
    f32 sweep = W.scan * (1.0f - v->menv) * (f32)(WT_WAVES - 1) * (0.35f + 0.65f * b->menv_amt);
    return clampf(W.pos + sweep, 0.0f, (f32)(WT_WAVES - 1));
}

static void wave_note(Voice *v, const Blk *b)
{
    int i;
    if (v->env < 0.001f) {
        for (i = 0; i < 4; i++) v->ph[i] = 0.0f;
        for (i = 0; i < 16; i++) v->s[i] = 0.0f;         /* (both sides start together: SPREAD sets them apart) */
    }
    v->s[4] = wave_place(v, b);
}

/* a wave at x (0 <= x < its length), between samples */
static inline f32 wt_at(const int16_t *t, f32 x)
{
    int i = (int)x;
    f32 a = (f32)t[i];
    return a + ((f32)t[i + 1] - a) * (x - (f32)i);
}

static void wave_render(Voice *v, const Blk *b, f32 *out_l, f32 *out_r, int n)
{
    f32 pitch = v->pitch + b->bend, f, incl, incr, pl, pr, ml, mr, fc, len;
    f32 xl = v->ph[0], xr = v->ph[1], off = 0.5f * W.spread * v->r1;   /* (the right side read further in) */
    const int16_t *tal, *tbl, *tar, *tbr;
    int ial, iar, lev = 0, i;
    Svf fl;

    if (b->punch > 0.01f) pitch += b->punch * v->menv * 3.0f;
    f = clampf(mtof(pitch), 8.0f, 16000.0f);
    incl = minf(f * (1.0f - W.det) * b->isr, 0.45f);         /* (turns a sample) */
    incr = minf(f * (1.0f + W.det) * b->isr, 0.45f);

    /* the level: the most harmonics whose highest is under ~18 kHz (the wider side decides) */
    while (lev < WT_LEVELS - 1 && (f32)(WT_H >> lev) * f * (1.0f + W.det) > 18000.0f)
        lev++;
    len = (f32)wave_len[lev];

    /* the place, smoothed a block at a time; the right side further along with SPREAD */
    v->s[4] += (wave_place(v, b) - v->s[4]) * 0.3f;
    pl = v->s[4];
    pr = minf((f32)(WT_WAVES - 1), pl + 2.2f * W.spread);
    ial = imin((int)pl, WT_WAVES - 2);
    iar = imin((int)pr, WT_WAVES - 2);
    ml = pl - (f32)ial;
    mr = pr - (f32)iar;
    tal = wave_pcm + (W.algo * WT_WAVES + ial) * WT_STRIDE + wave_off[lev]; tbl = tal + WT_STRIDE;
    tar = wave_pcm + (W.algo * WT_WAVES + iar) * WT_STRIDE + wave_off[lev]; tbr = tar + WT_STRIDE;

    fc = clampf(W.cut * exp2fx(v->menv * b->menv_amt * 3.4f + (v->vel_f - 0.5f) * b->vel_sens * 1.6f),
                60.0f, 18500.0f);
    svf_set(&fl, fc, W.q, b->isr);

    for (i = 0; i < n; i++) {
        f32 a, yl, yr, lp, bp, x;
        xl += incl; if (xl >= 1.0f) xl -= 1.0f;
        xr += incr; if (xr >= 1.0f) xr -= 1.0f;
        x = xl * len;
        a = wt_at(tal, x);
        yl = (a + (wt_at(tbl, x) - a) * ml) * wave_unit;
        x = xr + off; if (x >= 1.0f) x -= 1.0f;
        x *= len;
        a = wt_at(tar, x);
        yr = (a + (wt_at(tbr, x) - a) * mr) * wave_unit;
        yr = yl + (yr - yl) * W.spread;                      /* (blended in with SPREAD: width over the whole turn) */
        SVF_TICK(fl, &v->s[8], yl, lp, bp); (void)bp; out_l[i] = lp;
        SVF_TICK(fl, &v->s[10], yr, lp, bp); (void)bp; out_r[i] = lp;
    }
    v->ph[0] = xl; v->ph[1] = xr;
}

/* ---- picture: the wave now, with the bank's next waves stacked behind it --------------------------- */
static f32 wv_lev, wv_place;

/* wave w at sample j of its fullest level, morphed m of the way to the next */
static f32 wave_shape(const int16_t *bank, int w, f32 m, int j)
{
    const int16_t *a = bank + w * WT_STRIDE, *c = a + WT_STRIDE;
    f32 ya = (f32)a[j], yb = w < WT_WAVES - 1 ? (f32)c[j] : ya;
    return ya + (yb - ya) * m;
}

static void wave_draw(const Vis *vis)
{
    const int16_t *bank = wave_pcm + (vis->algo & 7) * WT_WAVES * WT_STRIDE;
    f32 dt = vis->dt, place, cut = vis->k[3], xy[2 * 64];
    int layer, i;

    wv_lev += (minf(1.0f, vis->level * 3.4f) - wv_lev) * minf(1.0f, dt * 14.0f);
    place = vis->k[0] * (f32)(WT_WAVES - 1);
    wv_place += (place - wv_place) * minf(1.0f, dt * 10.0f);

    /* the stack: three waves behind, further up the bank, smaller and dimmer */
    for (layer = 3; layer >= 0; layer--) {
        f32 p = minf((f32)(WT_WAVES - 1), wv_place + (f32)layer * 1.7f);
        int w = imin((int)p, WT_WAVES - 1);
        f32 m = p - (f32)w, ox = 22.0f + (f32)layer * 9.0f, oy = 120.0f - (f32)layer * 15.0f;
        f32 sx = 150.0f - (f32)layer * 8.0f, amp = (layer ? 16.0f : 24.0f + 8.0f * wv_lev), pk = 1e-3f;
        for (i = 0; i < 64; i++) {                           /* (each wave drawn to the same height) */
            xy[2 * i] = ox + sx * (f32)i * (1.0f / 63.0f);
            xy[2 * i + 1] = wave_shape(bank, w, m, i * 4);
            pk = maxf(pk, absf(xy[2 * i + 1]));
        }
        for (i = 0; i < 64; i++)
            xy[2 * i + 1] = oy - xy[2 * i + 1] * (amp / pk);
        g_color(layer ? GC_SLATE : GC_BLUE);
        g_poly(xy, 64, 0, layer ? 1.2f : 2.4f, layer ? 0.55f - 0.1f * (f32)layer : 0.95f);
    }

    /* the bank: fifteen places (the main waves wider), the one being read lit */
    for (i = 0; i < WT_WAVES; i++) {
        f32 x = 34.0f + (f32)i * 12.0f, near = 1.0f - minf(1.0f, absf(wv_place - (f32)i)), hw = i & 1 ? 2.0f : 5.0f;
        g_color(near > 0.02f ? GC_GREEN : GC_SLATE);
        g_rect(x - hw, 160.0f, hw * 2.0f, 4.0f, (i & 1 ? 0.2f : 0.3f) + 0.65f * near);
    }

    /* SPREAD: the right side's place; FILTER: a bar along the right edge */
    g_color(GC_WHITE);
    g_disc(34.0f + minf((f32)(WT_WAVES - 1), wv_place + 2.2f * vis->k[2]) * 12.0f, 170.0f, 2.4f, 0.8f);
    g_color(GC_ORANGE);
    g_line(214.0f, 158.0f, 214.0f, 158.0f - 110.0f * cut, 2.6f, 0.85f);
    g_color(GC_WHITE);
}

const InstDef inst_wave = {
    "WAVE",
    {59, 158, 255},
    {"WAVE", "SCAN", "SPREAD", "FILTER"},
    {"HARMON", "ORGAN", "VOX", "DIGI", "PULSE", "SYNC", "GLASS", "GROWL"},
    {
        {30, 20, 30, 70},            /* K1..K4 defaults */
        0,                           /* algo: HARMON */
        {4, 40, 70, 38},             /* Amp ADSR */
        {60, 46, 60, 0},             /* Mod/Filter Env */
        {0, 40, 0, 1},               /* LFO */
        {0, 3, 1, 65},               /* Arp */
        {0, 50, 34}                  /* Mix */
    },
    0.80f,
    wave_block, wave_note, wave_render, 0, wave_draw, 0
};
