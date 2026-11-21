/* A22 - CHIP: an 8-bit console voice.
 *
 * Pulse waves with four-bit duty steps, a stepped sixteen-level triangle, a linear-feedback noise register
 * (long and short), fast chord arpeggios and pitch sweeps, then CRUSH: fewer bits and a held sample rate.
 * Everything a note needs lives in its own voice (the noise register, the arpeggio's clock), so the second
 * core renders it the same as the first. The pulses are band-limited (polyBLEP); the stepped waves are not,
 * on purpose, and a gentle low-pass keeps their top end from turning to grit at high notes.
 *
 *   K1 (BLUE)   DUTY   pulse width in sixteenths (thin to square); NOISE: long or short register
 *   K2 (GREEN)  SWEEP  pitch drop at the start of a note (zaps, kicks, bleeps)
 *   K3 (WHITE)  ARP    arpeggio rate (ARP), vibrato depth (LEAD, PULSE), noise pitch (DRUM)
 *   K4 (ORANGE) CRUSH  fewer bits and a lower sample rate */
#include "internal.h"
#include "dsp.h"

static struct {
    int algo;
    f32 duty, sweep, arp, crush;
    f32 levels, ilevels;          /* CRUSH's quantizer: 2^bits steps */
    int hold;                     /* CRUSH's sample-and-hold: samples a value is kept */
} C;

static void chip_block(const Blk *b)
{
    f32 bits;
    C.algo  = b->algo & 7;
    C.duty  = (f32)(1 + (int)(b->k[0] * 7.99f)) * (1.0f / 16.0f);   /* 1/16 .. 8/16 */
    C.sweep = b->k[1];
    C.arp   = b->k[2];
    C.crush = b->k[3];
    bits = 8.0f - 5.0f * C.crush;                                   /* 8 bits .. 3 */
    C.levels = exp2fx(bits - 1.0f);
    C.ilevels = 1.0f / C.levels;
    C.hold = 1 + (int)(C.crush * C.crush * 9.0f);                   /* 44.1 kHz .. 4.4 kHz */
}

static void chip_note(Voice *v, const Blk *b)
{
    int i;
    (void)b;
    if (v->env < 0.001f) {
        for (i = 0; i < 4; i++) v->ph[i] = 0.0f;
        for (i = 0; i < 16; i++) v->s[i] = 0.0f;
        v->u[0] = 0x7FFFu;                                          /* the noise register, all ones */
    }
    v->s[3] = 1.0f;                                                 /* the sweep starts again */
}

/* one step of the 15-bit noise register: long (bits 0 and 1) or short (0 and 6, a metallic 93-step loop) */
static inline uint32_t lfsr_step(uint32_t r, int short_mode)
{
    uint32_t fb = (r ^ (r >> (short_mode ? 6 : 1))) & 1u;
    return ((r >> 1) | (fb << 14)) & 0x7FFFu;
}

/* the triangle in sixteen steps, as the consoles stored it */
static inline f32 tri16(f32 ph)
{
    f32 t = tri_wave(ph);
    return (f32)(int)((t + 1.0f) * 7.5f + 0.5f) * (2.0f / 15.0f) - 1.0f;
}

static void chip_render(Voice *v, const Blk *b, f32 *out_l, f32 *out_r, int n)
{
    static const int8_t ARP3[4] = {0, 4, 7, 12};                    /* major: root, third, fifth, octave */
    int algo = C.algo, i, hold = C.hold, cnt = (int)v->s[5], arp_i;
    uint32_t r = v->u[0] ? v->u[0] : 0x7FFFu;
    f32 pitch = v->pitch + b->bend, f, d0, d1, dn, vib, held = v->s[6], lp = v->s[8];
    f32 p0 = v->ph[0], p1 = v->ph[1], pn = v->ph[2];
    f32 nv = v->s[0], drum = 0.0f;
    f32 sweep = v->s[3];

    /* the sweep: a drop from above that dies in ~40..160 ms; DRUM always sweeps */
    if (algo == 6) pitch += sweep * (14.0f + 22.0f * C.sweep);
    else pitch += sweep * C.sweep * 24.0f;
    v->s[3] = sweep * (1.0f - onepole_k(0.02f + 0.06f * (1.0f - C.sweep), b->sr / (f32)n));

    /* ARP: the chord's next note every 1/60 .. 1/12 s, counted from this voice's own age */
    if (algo == 3) {
        f32 rate = 12.0f + 48.0f * C.arp;
        arp_i = (int)(v->age * rate) & 3;
        pitch += (f32)ARP3[arp_i];
    }
    /* vibrato after a short delay (LEAD and PULSE) */
    vib = (algo == 5 || algo == 0) ? C.arp * 0.6f * sat((v->age - 0.18f) * 4.0f) * tsin(v->age * 5.5f) : 0.0f;
    pitch += vib;
    if (algo == 4) pitch -= 12.0f;                                  /* BASS an octave down */

    f = clampf(mtof(pitch), 16.0f, 12000.0f);
    d0 = minf(f * b->isr, 0.45f);
    d1 = minf(d0 * (algo == 4 ? 0.5f : 1.0f), 0.45f);
    /* the noise register's clock: with the note (NOISE), or K3 for DRUM's snare */
    dn = algo == 6 ? minf((800.0f + 7000.0f * C.arp) * b->isr, 1.0f) : minf(f * 4.0f * b->isr, 1.0f);
    if (algo == 6) drum = sat(1.0f - v->age * (6.0f - 4.0f * C.sweep));

    for (i = 0; i < n; i++) {
        f32 x;
        pn += dn;
        while (pn >= 1.0f) {                                        /* the register steps at its own clock */
            pn -= 1.0f;
            r = lfsr_step(r, algo == 2 && C.duty > 0.3f);
            nv = (r & 1u) ? 0.7f : -0.7f;
        }
        switch (algo) {
        case 0: /* PULSE */
        case 3: /* ARP */
        case 5: /* LEAD: its duty sweeps slowly while held */
        {
            f32 duty = algo == 5 ? clampf(C.duty + 0.18f * tsin(v->age * 0.7f), 0.06f, 0.94f) : C.duty;
            x = 0.62f * pulse_bl(p0, d0, duty);
            break;
        }
        case 1: /* TRI */
            x = 0.85f * tri16(p0);
            break;
        case 2: /* NOISE */
            x = nv;
            break;
        case 4: /* BASS: a pulse an octave down over a stepped triangle another below */
            x = 0.48f * pulse_bl(p0, d0, C.duty) + 0.55f * tri16(p1);
            break;
        case 6: /* DRUM: a falling triangle and a noise burst */
            x = 0.8f * tri16(p0) * (0.4f + 0.6f * drum) + nv * 0.75f * drum * drum;
            break;
        default: /* DUET: two pulses a fifth apart, one of them thin */
            x = 0.42f * pulse_bl(p0, d0, C.duty) + 0.34f * pulse_bl(p1, minf(d0 * 1.4983f, 0.45f), 0.125f);
            break;
        }
        p0 += d0; if (p0 >= 1.0f) p0 -= 1.0f;
        p1 += algo == 7 ? minf(d0 * 1.4983f, 0.45f) : d1; if (p1 >= 1.0f) p1 -= 1.0f;

        /* CRUSH: hold a value for `hold` samples, in fewer steps */
        if (++cnt >= hold) {
            cnt = 0;
            held = (f32)(int)(x * C.levels + (x >= 0.0f ? 0.5f : -0.5f)) * C.ilevels;
        }
        lp += (held - lp) * 0.72f;                                 /* (about 11 kHz: the stepped waves' edge) */
        out_l[i] = lp;
        out_r[i] = lp;
    }
    v->ph[0] = p0; v->ph[1] = p1; v->ph[2] = pn;
    v->s[0] = nv; v->s[5] = (f32)cnt; v->s[6] = held; v->s[8] = lp;
    v->u[0] = r;
}

/* ---- picture: a pixel screen with the wave in steps, and the four channels' meters ------------------ */
static f32 cp_lev, cp_blink;
static uint32_t cp_seen;

static void chip_draw(const Vis *vis)
{
    static const char *const CH[4] = {"PU1", "PU2", "TRI", "NOI"};
    static const int CC[4] = {GC_BLUE, GC_GREEN, GC_WHITE, GC_ORANGE};
    f32 dt = vis->dt, duty = (f32)(1 + (int)(vis->k[0] * 7.99f)) * (1.0f / 16.0f);
    f32 crush = vis->k[3], x0 = 30.0f, y0 = 50.0f, cell = 6.0f;
    int algo = vis->algo & 7, steps = 30, i, act[4] = {0, 0, 0, 0};

    if (vis->fresh) cp_seen = vis->notes;
    if (vis->notes != cp_seen) { cp_seen = vis->notes; cp_blink = 1.0f; }
    cp_blink = maxf(0.0f, cp_blink - dt * 4.0f);
    cp_lev += (minf(1.0f, vis->level * 3.4f) - cp_lev) * minf(1.0f, dt * 14.0f);

    /* the screen's frame */
    g_color(GC_SLATE);
    g_box(x0 - 6.0f, y0 - 6.0f, 192.0f, 84.0f, 1.4f, 0.6f);

    /* the wave, one cycle and a half, in pixel cells; CRUSH makes the cells coarser */
    {
        int lv = 4 + (int)((1.0f - crush) * 6.0f);                 /* levels drawn: 10 .. 4 */
        g_color(algo == 2 || algo == 6 ? GC_ORANGE : GC_GREEN);
        for (i = 0; i < steps; i++) {
            f32 ph = fractf((f32)i * (1.5f / (f32)steps)), y;
            if (algo == 1 || algo == 4) y = tri_wave(ph);
            else if (algo == 2) y = (((uint32_t)i * 2654435761u) >> 29 & 1u) ? 0.8f : -0.8f;
            else if (algo == 6) y = tri_wave(ph) * (1.0f - (f32)i / (f32)steps);
            else y = ph < duty ? 0.9f : -0.9f;
            y = (f32)(int)(y * (f32)lv * 0.5f + (f32)lv * 0.5f) / (f32)lv;
            g_rect(x0 + (f32)i * cell, y0 + 66.0f - y * 62.0f, cell - 1.0f, cell - 1.0f,
                   0.55f + 0.4f * cp_lev);
        }
    }

    /* four channel meters: the voices sounding, shared out over the channels the algorithm uses */
    for (i = 0; i < NVOICE; i++)
        if (vis->v[i].on) {
            int c = algo == 1 ? 2 : (algo == 2 ? 3 : (algo == 6 ? 2 + (i & 1) : (i & 1)));
            act[c]++;
        }
    for (i = 0; i < 4; i++) {
        f32 x = 34.0f + (f32)i * 46.0f, h = minf(1.0f, (f32)act[i] * 0.34f) * (0.5f + 0.5f * cp_lev);
        g_color(CC[i]);
        g_box(x, 146.0f, 34.0f, 14.0f, 1.2f, 0.5f);
        if (h > 0.01f) g_rect(x + 2.0f, 148.0f, 30.0f * h, 10.0f, 0.85f);
        g_color(GC_SLATE);
        g_text(x + 17.0f, 166.0f, CH[i], 7.0f, 1.0f, 0.6f, 1);
    }
    if (cp_blink > 0.0f) {
        g_color(GC_GOLD);
        g_rect(x0 + 176.0f, y0 - 2.0f, 6.0f, 6.0f, cp_blink);
    }
    g_color(GC_WHITE);
}

const InstDef inst_chip = {
    "CHIP",
    {68, 227, 126},
    {"DUTY", "SWEEP", "ARP", "CRUSH"},
    {"PULSE", "TRI", "NOISE", "ARP", "BASS", "LEAD", "DRUM", "DUET"},
    {
        {50, 0, 40, 10},             /* K1..K4 defaults */
        0,                           /* algo: PULSE */
        {0, 30, 70, 18},             /* Amp ADSR: square-edged */
        {50, 30, 50, 0},             /* Mod/Filter Env */
        {0, 40, 0, 1},               /* LFO */
        {0, 3, 1, 65},               /* Arp */
        {0, 50, 20}                  /* Mix */
    },
    0.62f,
    chip_block, chip_note, chip_render, 0, chip_draw, 0
};
