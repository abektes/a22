/* A22 - WAVE's wavetables (core/wave_data.c, written by tools/gen_wavetables.py) */
#pragma once
#include "mathx.h"

#define WT_N 128                 /* samples a cycle */
#define WT_BANKS 8
#define WT_WAVES 8
extern const f32 wave_tab[WT_BANKS * WT_WAVES][WT_N + 1];
