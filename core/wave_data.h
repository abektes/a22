/* A22 - WAVE's wavetables (core/wave_data.c, written by tools/gen_wavetables.py) */
#pragma once
#include "mathx.h"

#define WT_N 256                 /* samples a cycle at level 0 */
#define WT_H 48                  /* harmonics at level 0; level k keeps WT_H >> k */
#define WT_BANKS 8
#define WT_WAVES 15              /* a bank: eight main waves (the even places) and seven between */
#define WT_LEVELS 6
#define WT_STRIDE 550            /* samples a wave over all its levels */

/* wave w (bank * WT_WAVES + place) at level k: wave_len[k] samples and one more (the first again), from
 * wave_pcm[w * WT_STRIDE + wave_off[k]]; times wave_unit for the generator's units */
extern const f32 wave_unit;
extern const uint16_t wave_len[WT_LEVELS];
extern const uint16_t wave_off[WT_LEVELS];
extern const int16_t wave_pcm[WT_BANKS * WT_WAVES * WT_STRIDE];
