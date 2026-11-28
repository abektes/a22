/* Writes tests/state_v5.bin: a version 5 saved state, as A22 0.5.0 to 0.8.0 and the chord pads' first sprint save it,
 * with the chord and key settings away from their defaults, for tests/state.c to load. Version 6 only appends to
 * version 5, so the state's first and last tables are marked too. It is built against a version 5 core:
 *   mkdir -p build/v5 && git archive 49b6cee core | tar -x -C build/v5
 *   cc -std=c99 -w -Ibuild/v5/core build/v5/core/*.c tests/state_v5_gen.c -o build/state_v5_gen -lm
 *   build/state_v5_gen tests/state_v5.bin */
#include <stdio.h>
#include "internal.h"
#include "h1.h"

int main(int argc, char **argv)
{
    uint32_t size;
    const void *p;
    FILE *f;
    if (argc < 2) return 2;
    h1_init(44100);
    if (st.version != 5u) { printf("not version 5's core\n"); return 1; }
    st.octave = -1;
    st.keys[0] = 5; st.keys[1] = 3; st.keys[2] = 40; st.keys[3] = 1;
    st.glo[0] = 133;
    st.chord[0] = CHORD_PADS; st.chord[1] = 0; st.chord[2] = VOICE_SMOOTH; st.chord[3] = 20;
    st.chord_n = 3; st.chord_mem[0] = 0; st.chord_mem[1] = 4; st.chord_mem[2] = 7;
    st.rsv[0] = 0x85;                            /* the step pattern's key */
    st.rsv[1] = 10;                              /* the chord pads' set */
    st.trk[2].vol = 61;
    st.steps[NSTEP - 1][NSNOTE - 1] = 60;
    st.step_trk[NSTEP - 1][NSNOTE - 1] = 2;
    st.step_vel[NSTEP - 1][NSNOTE - 1] = 99;     /* (the state's last byte) */
    p = h1_state(&size);
    if (size != 2888u) { printf("unexpected size %u\n", (unsigned)size); return 1; }
    f = fopen(argv[1], "wb");
    if (!f || fwrite(p, 1, size, f) != size || fclose(f)) { printf("cannot write %s\n", argv[1]); return 1; }
    printf("wrote %u bytes to %s\n", (unsigned)size, argv[1]);
    return 0;
}
