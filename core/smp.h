/* h1 - the recordings (smp_data.c, cut by tools/samples/build.py) and what plays them.
 *
 * A bank is a few zones up the keyboard, each cut from one recorded note, stored as 8-bit mu-law at
 * 22.05 kHz. There are two kinds. A plucked bank holds what a string was fed in the first tenth of a
 * second of the note (the pluck and the body's answer to it), and a string of str.h does the rest, with
 * the loss the zone was fitted with. A held bank holds a note's start and a stretch of it that repeats.
 * Both are read the same way, by smp_read, faster for a higher note and slower for a lower one.
 *
 * A zone is stored with its highs lifted (y[n] - pre * y[n - 1]) and the reader takes them down again,
 * which takes the 8 bits' noise down with them: under a dull recording it would be a hiss. */
#pragma once
#include "internal.h"
#include "dsp.h"
#include "smp_data.h"

#define SMP_RATE 22050.0f

typedef struct SmpZone {
    uint32_t off;                /* into smp_data */
    uint16_t len;                /* stored samples */
    uint16_t loop;               /* a held zone goes round from here to the end; a plucked one has loop == len */
    f32 key;                     /* the MIDI note the recording is, played at its own speed */
    f32 gain;                    /* makes every zone as loud as the next */
    f32 g, t60;                  /* plucked: the string's loop that gives the recording's decay (str.h) */
    f32 pre;                     /* how far its highs were lifted */
} SmpZone;

typedef struct SmpBank {
    uint8_t first, n;            /* its zones, low to high */
} SmpBank;

extern const f32 smp_mu[256];
extern const SmpZone smp_zones[];
extern const SmpBank smp_banks[SMP_NBANK];
extern const uint8_t smp_data[];

/* the zone of a bank nearest a pitch, as its number in smp_zones */
int smp_pick(int bank, f32 pitch);

/* Zone `zone` if it is one of the bank's, or else the bank's nearest to the pitch. (A voice keeps its zone's
 * number, and a voice that was started on another algorithm has another bank's.) */
const SmpZone *smp_zone(int bank, uint32_t zone, f32 pitch);

/* A plucked zone's fitted string at a pitch: the zone's own, as far as half an octave above its recording
 * (what it is fed was cut to fit that string, and is played faster or slower with the note); further up,
 * where the bank has no more recordings, shorter and shorter, with less of its top lost each time round. */
void smp_fit(const SmpZone *z, f32 pitch, f32 *g, f32 *t60);

/* How far a zone moves on per engine sample to sound at `pitch`, in 1/65536 of a stored sample. */
static inline uint32_t smp_inc(const SmpZone *z, f32 pitch, f32 isr)
{
    f32 r = exp2fx(clampf((pitch - z->key) * (1.0f / 12.0f), -5.0f, 2.9f)) * SMP_RATE * isr;
    return (uint32_t)(r * 65536.0f);
}

/* From the start of a zone. */
static inline void smp_start(SmpR *r)
{
    r->frac = r->idx = 0;
    r->w[0] = r->w[1] = r->w[2] = r->w[3] = 0.0f;
}

/* A plucked zone that has been read to its end. */
static inline int smp_done(const SmpZone *z, const SmpR *r)
{
    return z->loop >= z->len && r->idx >= z->len && r->w[0] == 0.0f && r->w[1] == 0.0f && r->w[2] == 0.0f && r->w[3] == 0.0f;
}

/* The next n samples of a zone at speed inc, times gain, written to out or added to it. After a plucked
 * zone's end there is silence. (r may be anything, a reader left in another zone too: it is put right.) */
void smp_read(const SmpZone *z, SmpR *r, uint32_t inc, f32 gain, f32 *out, int n, int add);
