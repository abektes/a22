/* Writes tests/state_v4.bin: a version 4 saved state, as A22 0.1.0 to 0.4.0 saved it, with every table
 * away from its defaults (and every value inside what the loader keeps), for tests/state.c to load.
 * It is built against 0.4.0's own core, not this one:
 *   mkdir -p build/v040 && git archive v0.4.0 core | tar -x -C build/v040
 *   cc -std=c99 -w -Ibuild/v040/core build/v040/core/*.c tests/state_v4_gen.c -o build/state_v4_gen -lm
 *   build/state_v4_gen tests/state_v4.bin */
#include <stdio.h>
#include "internal.h"
#include "h1.h"

int main(int argc, char **argv)
{
    int i, j, a, k;
    uint32_t size;
    const void *p;
    FILE *f;
    if (argc < 2) return 2;
    h1_init(44100);
    if (st.version != 4u) { printf("not version 4's core\n"); return 1; }
    st.inst = 10; st.track = 2; st.octave = -1; st.hud = 0;
    st.keys[0] = 5; st.keys[1] = 3; st.keys[2] = 40; st.keys[3] = 1;
    st.glo[0] = 133; st.glo[1] = 21; st.glo[2] = 47; st.glo[3] = 66;
    st.seq[1] = 3; st.seq[2] = 2; st.seq[3] = 2; st.seq[4] = 0; st.seq[5] = CLICK_OFF; st.seq[7] = 0;
    st.tape[0] = TAPE_SPD_2X; st.tape[1] = 2; st.tape[2] = 6;
    st.fx_type = FX_TANK; st.fx_on = 0;
    for (i = 0; i < NFX; i++)
        for (k = 0; k < 4; k++)
            st.fx_p[i][k] = (uint8_t)((i * 4 + k) * 3 % 100);
    st.fx_p[FX_DELAY][1] = 5;                    /* (TIME is a choice of 0..7) */
    for (k = 0; k < 4; k++) { st.master[k] = (uint8_t)(30 + k * 11); st.shake[k] = (uint8_t)(20 + k * 13); }
    st.riff_len = 5; st.riff_root = 62;
    for (i = 0; i < 5; i++) st.riff[i] = (uint8_t)(61 + i * 2);
    st.chord[0] = CHORD_MEMORY; st.chord[1] = 2; st.chord[2] = 1; st.chord[3] = 30;
    st.chord_n = 4; st.chord_mem[1] = 3; st.chord_mem[2] = 7; st.chord_mem[3] = 10;
    for (i = 0; i < NINST; i++) {
        InstParams *ip = &st.ip[i];
        ip->algo = (uint8_t)(i % 8);
        for (k = 0; k < 4; k++) {
            ip->knob0[k] = (uint8_t)((i * 9 + k * 5) % 100);
            ip->env[k] = (uint8_t)((i * 7 + k * 13) % 100);
            ip->menv[k] = (uint8_t)((i * 11 + k * 3) % 100);
        }
        ip->lfo[0] = (uint8_t)(i % 7); ip->lfo[1] = (uint8_t)(i * 8); ip->lfo[2] = (uint8_t)(i * 5); ip->lfo[3] = (uint8_t)(i % 7);
        ip->arp[0] = (uint8_t)(i % NARPMODE); ip->arp[1] = (uint8_t)(i % 6); ip->arp[2] = (uint8_t)(1 + i % 4);
        ip->arp[3] = (uint8_t)(10 + i * 7);
        ip->mix[IP_CUT] = (uint8_t)(i * 3); ip->mix[IP_PAN] = (uint8_t)(20 + i * 5); ip->mix[IP_SEND] = (uint8_t)(i * 6);
        for (a = 0; a < NALGO; a++)
            for (k = 0; k < 4; k++)
                st.knob[i][a][k] = (uint8_t)((i * 31 + a * 7 + k * 3) % 101);
    }
    for (i = 0; i < NTRACK; i++) {
        Track *t = &st.trk[i];
        t->inst = (uint8_t)(11 - i); t->algo = (uint8_t)(7 - i); t->vol = (uint8_t)(60 + i * 9);
        t->pan = (uint8_t)(30 + i * 10); t->send = (uint8_t)(i * 20); t->mute = (uint8_t)(i & 3);
        for (k = 0; k < 4; k++) {
            t->knob[k] = (uint8_t)(i * 20 + k); t->env[k] = (uint8_t)(i * 10 + k * 2);
            t->menv[k] = (uint8_t)(90 - i * 10 - k);
        }
        t->lfo[0] = (uint8_t)i; t->lfo[1] = 44; t->lfo[2] = (uint8_t)(i * 9); t->lfo[3] = (uint8_t)(i + 1);
        t->has_snap = (uint8_t)(i & 1);
    }
    for (i = 0; i < NSNAP; i++) {
        Snap *s = &st.snap[i];
        s->used = (uint8_t)(i % 3 != 0); s->inst = (uint8_t)(i + 3); s->algo = (uint8_t)(7 - i); s->fx_type = (uint8_t)i;
        for (k = 0; k < 4; k++) {
            s->knob[k] = (uint8_t)(i * 12 + k); s->env[k] = (uint8_t)(i * 6 + k); s->menv[k] = (uint8_t)(i * 9 + k);
            s->fx_p[k] = (uint8_t)(i * 11 + k);
        }
        s->lfo[0] = (uint8_t)(i % 7); s->lfo[1] = (uint8_t)(i * 10); s->lfo[2] = (uint8_t)(i * 4); s->lfo[3] = (uint8_t)(i % 7);
    }
    for (i = 0; i < NSTEP; i++)
        for (j = 0; j < NSNOTE; j++)
            if ((i + j) % 5 == 0) {
                st.steps[i][j] = (uint8_t)(48 + (i * 3 + j) % 40);
                st.step_trk[i][j] = (uint8_t)(j % NTRACK);
                st.step_vel[i][j] = (uint8_t)(40 + (i + j * 7) % 87);
            }
    p = h1_state(&size);
    if (size != 2648u) { printf("unexpected size %u\n", (unsigned)size); return 1; }
    f = fopen(argv[1], "wb");
    if (!f || fwrite(p, 1, size, f) != size) return 1;
    fclose(f);
    printf("wrote %s, %u bytes, version %u\n", argv[1], (unsigned)size, (unsigned)st.version);
    return 0;
}
