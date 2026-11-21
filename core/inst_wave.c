/* A22 - WAVE: a wavetable synthesizer.
 *
 * Each algorithm is a bank of eight single-cycle waves (core/wave_data.c, made by tools/gen_wavetables.py);
 * K1 morphs through them. The tables are stored integrated and read twice a step: the difference over the
 * step, divided by its length, is the wave averaged over that step (integrated wavetable synthesis), which
 * keeps the bright banks from aliasing with no per-octave copies. Two oscillators, left and right, can be
 * detuned and read a little apart in the bank for width, then a resonant low-pass per side.
 *
 *   K1 (BLUE)   WAVE    place in the bank, morphing smoothly from wave to wave
 *   K2 (GREEN)  SCAN    how far the place moves up the bank as the mod envelope dies away (evolving notes)
 *   K3 (WHITE)  SPREAD  stereo: the right side detuned and further along the bank
 *   K4 (ORANGE) FILTER  low-pass cutoff (Mod-Env and velocity open it) */
#include "internal.h"
#include "dsp.h"
#include "wave_data.h"

static struct {
    int algo;
    f32 pos, scan, spread, cut, q;
} W;

static void wave_block(const Blk *b)
{
    W.algo   = b->algo & 7;
    W.pos    = b->k[0] * (f32)(WT_WAVES - 1);
    W.scan   = b->k[1];
    W.spread = b->k[2];
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
        for (i = 0; i < 16; i++) v->s[i] = 0.0f;
        v->ph[1] = v->r1 * (f32)WT_N;                        /* (the right side starts apart: no comb) */
        v->q[0] = 0;                                         /* the integrals below are for no waves yet */
    }
    v->s[4] = wave_place(v, b);
}

/* the integral of a wave at x (0 <= x < WT_N), between table entries */
static inline f32 wt_at(const f32 *t, f32 x)
{
    int i = (int)x;
    return t[i] + (t[i + 1] - t[i]) * (x - (f32)i);
}

static void wave_render(Voice *v, const Blk *b, f32 *out_l, f32 *out_r, int n)
{
    const f32 *bank = wave_tab[W.algo * WT_WAVES];
    f32 pitch = v->pitch + b->bend, f, incl, incr, iil, iir, pl, pr, ml, mr, fc;
    f32 xl = v->ph[0], xr = v->ph[1];
    f32 al0 = v->s[0], bl0 = v->s[1], ar0 = v->s[2], br0 = v->s[3];
    const f32 *tal, *tbl, *tar, *tbr;
    int ial, iar, i;
    uint32_t key;
    Svf fl;

    if (b->punch > 0.01f) pitch += b->punch * v->menv * 3.0f;
    f = clampf(mtof(pitch), 8.0f, 16000.0f);
    incl = minf(f * (1.0f - 0.0035f * W.spread) * (f32)WT_N * b->isr, (f32)WT_N * 0.45f);
    incr = minf(f * (1.0f + 0.0035f * W.spread) * (f32)WT_N * b->isr, (f32)WT_N * 0.45f);
    iil = 1.0f / incl;
    iir = 1.0f / incr;

    /* the place, smoothed a block at a time; the right side further along with SPREAD */
    v->s[4] += (wave_place(v, b) - v->s[4]) * 0.3f;
    pl = v->s[4];
    pr = minf((f32)(WT_WAVES - 1), pl + 1.1f * W.spread);
    ial = imin((int)pl, WT_WAVES - 2);
    iar = imin((int)pr, WT_WAVES - 2);
    ml = pl - (f32)ial;
    mr = pr - (f32)iar;
    tal = bank + ial * (WT_N + 1); tbl = tal + (WT_N + 1);
    tar = bank + iar * (WT_N + 1); tbr = tar + (WT_N + 1);

    /* moved on to other waves: their integrals at where the oscillators are, so the first step is right */
    key = (uint32_t)(ial + 1) | (uint32_t)(iar + 1) << 8 | (uint32_t)(W.algo + 1) << 16;
    if (v->q[0] != key) {
        v->q[0] = key;
        al0 = wt_at(tal, xl); bl0 = wt_at(tbl, xl);
        ar0 = wt_at(tar, xr); br0 = wt_at(tbr, xr);
    }

    fc = clampf(W.cut * exp2fx(v->menv * b->menv_amt * 3.4f + (v->vel_f - 0.5f) * b->vel_sens * 1.6f),
                60.0f, 18500.0f);
    svf_set(&fl, fc, W.q, b->isr);

    for (i = 0; i < n; i++) {
        f32 a1, b1, yl, yr, lp, bp;
        xl += incl; if (xl >= (f32)WT_N) xl -= (f32)WT_N;
        xr += incr; if (xr >= (f32)WT_N) xr -= (f32)WT_N;
        a1 = wt_at(tal, xl); b1 = wt_at(tbl, xl);
        yl = ((a1 - al0) + ((b1 - bl0) - (a1 - al0)) * ml) * iil;
        al0 = a1; bl0 = b1;
        a1 = wt_at(tar, xr); b1 = wt_at(tbr, xr);
        yr = ((a1 - ar0) + ((b1 - br0) - (a1 - ar0)) * mr) * iir;
        ar0 = a1; br0 = b1;
        SVF_TICK(fl, &v->s[8], yl, lp, bp); (void)bp; out_l[i] = lp;
        SVF_TICK(fl, &v->s[10], yr, lp, bp); (void)bp; out_r[i] = lp;
    }
    v->ph[0] = xl; v->ph[1] = xr;
    v->s[0] = al0; v->s[1] = bl0; v->s[2] = ar0; v->s[3] = br0;
}

/* ---- picture: the wave now, with the bank's next waves stacked behind it --------------------------- */
static f32 wv_lev, wv_place;

/* the wave itself (not its integral) at sample j of wave w, morphed m of the way to the next */
static f32 wave_shape(const f32 *bank, int w, f32 m, int j)
{
    const f32 *a = bank + w * (WT_N + 1), *c = a + (WT_N + 1);
    f32 ya = a[j + 1] - a[j], yb = w < WT_WAVES - 1 ? c[j + 1] - c[j] : ya;
    return ya + (yb - ya) * m;
}

static void wave_draw(const Vis *vis)
{
    const f32 *bank = wave_tab[(vis->algo & 7) * WT_WAVES];
    f32 dt = vis->dt, place, cut = vis->k[3], xy[2 * 64];
    int layer, i;

    wv_lev += (minf(1.0f, vis->level * 3.4f) - wv_lev) * minf(1.0f, dt * 14.0f);
    place = vis->k[0] * (f32)(WT_WAVES - 1);
    wv_place += (place - wv_place) * minf(1.0f, dt * 10.0f);

    /* the stack: three waves behind, further up the bank, smaller and dimmer */
    for (layer = 3; layer >= 0; layer--) {
        f32 p = minf((f32)(WT_WAVES - 1), wv_place + (f32)layer * 0.85f);
        int w = imin((int)p, WT_WAVES - 1);
        f32 m = p - (f32)w, ox = 22.0f + (f32)layer * 9.0f, oy = 120.0f - (f32)layer * 15.0f;
        f32 sx = 150.0f - (f32)layer * 8.0f, amp = (layer ? 16.0f : 24.0f + 8.0f * wv_lev), pk = 1e-3f;
        for (i = 0; i < 64; i++) {                           /* (each wave drawn to the same height) */
            xy[2 * i] = ox + sx * (f32)i * (1.0f / 63.0f);
            xy[2 * i + 1] = wave_shape(bank, w, m, i * 2);
            pk = maxf(pk, absf(xy[2 * i + 1]));
        }
        for (i = 0; i < 64; i++)
            xy[2 * i + 1] = oy - xy[2 * i + 1] * (amp / pk);
        g_color(layer ? GC_SLATE : GC_BLUE);
        g_poly(xy, 64, 0, layer ? 1.2f : 2.4f, layer ? 0.55f - 0.1f * (f32)layer : 0.95f);
    }

    /* the bank: eight places, the one being read lit */
    for (i = 0; i < WT_WAVES; i++) {
        f32 x = 34.0f + (f32)i * 24.0f, near = 1.0f - minf(1.0f, absf(wv_place - (f32)i));
        g_color(near > 0.02f ? GC_GREEN : GC_SLATE);
        g_rect(x - 7.0f, 160.0f, 14.0f, 4.0f, 0.3f + 0.65f * near);
    }

    /* SPREAD: the right side's place; FILTER: a bar along the right edge */
    g_color(GC_WHITE);
    g_disc(34.0f + minf(7.0f, wv_place + 1.1f * vis->k[2]) * 24.0f, 170.0f, 2.4f, 0.8f);
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
