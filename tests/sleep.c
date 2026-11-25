/* 0.6.0's two savings that must not change the sound, held to the bit:
 *   - DELAY, SHIFT, CHORUS and TANK sleep once their line has run out, as PLATE does (core/fx.c, echo_asleep)
 *   - a WIRE note clears or damps only the delay it can read, not its whole row of 2,048 (core/inst_wire.c)
 * The same playing, hashed in sections: `sleep` as the firmware is, `sleep before` with both switched back to how
 * they were (fx_test_no_sleep, wire_test_whole_row). tests/run_tests.sh runs both and compares. The first also checks
 * that each of the four effects did fall asleep, or the comparison would prove nothing.
 *
 * The playing: each effect in turn, bursts of notes, silences of 3 to 10 s (long enough to sleep), knobs and tempo
 * moved while it sleeps, then woken; WIRE on every algorithm with glide, the LFO on pitch at full depth, quick
 * re-plucks (legato), and a take on the tape played at 0.5X, 2X and 1X with the pitch following. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/internal.h"
#ifdef __SSE2__
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif

/* Denormals flushed to zero, as the FM-1 does (port/host/main.c does the same): without it a filter's state can stop at
 * the smallest denormal instead of 0, and an effect never sleeps on the host though it does on the device. */
static void flush_denormals(void)
{
#if defined(__SSE2__)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#elif defined(__aarch64__)
    unsigned long fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr | (1ul << 24)));
#endif
}

static unsigned long long h;
static unsigned rng = 777u, ms;
static unsigned rnd(unsigned n) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) % n; }

static void run(int blocks)
{
    static int32_t b[512];
    int i, k;
    for (k = 0; k < blocks; k++) {
        h1_render(b, 256);
        for (i = 0; i < 512; i++) h = (h ^ (unsigned)b[i]) * 1099511628211ull;
        ms += 6;
        if (k % 3 == 0) h1_frame(ms);
    }
}

static void section(const char *name)
{
    printf("%-10s %016llx\n", name, h);
    h = 1469598103934665603ull;
}

static void effects(void)
{
    static const int FXS[4] = {FX_DELAY, FX_SHIFT, FX_CHORUS, FX_TANK};
    static const char *const NAMES[4] = {"DELAY", "SHIFT", "CHORUS", "TANK"};
    int f, r, i, t;
    for (f = 0; f < 4; f++) {
        for (r = 0; r < 6; r++) {
            st.fx_type = (uint8_t)FXS[f];
            st.fx_on = 1;
            for (i = 0; i < 4; i++) st.fx_p[FXS[f]][i] = (uint8_t)(20 + rnd(81));
            for (t = 0; t < NTRACK; t++) st.trk[t].send = 100;
            h1_encoder(H1_ENC_PRESET, (int)rnd(3));
            for (i = 0; i < 6; i++) {
                int k = (int)rnd(H1_NKEYS);
                h1_key(k, 1); run(2 + (int)rnd(20)); h1_key(k, 0); run((int)rnd(8));
            }
            run(500 + (int)rnd(1200));                       /* the line runs out: asleep */
            for (i = 0; i < 4; i++) st.fx_p[FXS[f]][i] = (uint8_t)rnd(101);   /* knobs moved while asleep */
            st.glo[0] = (uint8_t)(60 + rnd(150));           /* and the tempo (DELAY's time glides meanwhile) */
            run(100 + (int)rnd(300));
            h1_key(5, 1); run(3); h1_key(5, 0);             /* woken */
            run(300);
        }
        st.fx_type = (uint8_t)FXS[(f + 1) % 4];              /* another a moment, then back: it resumes */
        run(20);
        section(NAMES[f]);
    }
    st.fx_type = FX_PLATE;
}

static void wire(void)
{
    int a, r, i, t;
    char name[16];
    for (i = 0; i < NINST && strcmp(h1_instrument_name(h1_instrument()), "WIRE"); i++) {   /* WIRE */
        h1_encoder(H1_ENC_PRESET, 1);
        run(2);
    }
    if (strcmp(h1_instrument_name(h1_instrument()), "WIRE")) {
        fprintf(stderr, "FAIL sleep: PRESETS never reached WIRE\n");
        exit(1);
    }
    for (a = 0; a < 8; a++) {
        for (t = 0; t < NTRACK; t++) { st.trk[t].lfo[0] = 0; st.trk[t].lfo[1] = 80; st.trk[t].lfo[2] = 100; st.trk[t].lfo[3] = 0; }
        for (i = 0; i < NSLOT; i++) { st.ip[i].lfo[0] = 0; st.ip[i].lfo[1] = 80; st.ip[i].lfo[2] = 100; st.ip[i].lfo[3] = 0; }
        st.keys[2] = (uint8_t)(a & 1 ? 70 : 0);              /* glide on every other algorithm */
        for (r = 0; r < 120; r++) {
            int k = (int)rnd(H1_NKEYS);
            h1_key(k, 1);
            run(1 + (int)rnd(6));
            if (rnd(3)) h1_key(k, 0);
            if (rnd(4) == 0) { h1_key(k, 1); run(1); h1_key(k, 0); }   /* a quick re-pluck */
            run((int)rnd(4));
        }
        for (i = 0; i < H1_NKEYS; i++) h1_key(i, 0);
        run(60);
        h1_button(H1_BTN_REC, 1); h1_button(H1_BTN_REC, 0);   /* a take */
        run(5);
        for (r = 0; r < 24; r++) {
            int k = (int)rnd(H1_NKEYS);
            h1_key(k, 1); run(4 + (int)rnd(10)); h1_key(k, 0); run((int)rnd(5));
        }
        h1_button(H1_BTN_REC, 1); h1_button(H1_BTN_REC, 0);
        st.seq[7] = 1;                                       /* the tape's speed moves its pitch */
        for (i = 0; i < 6; i++) {
            st.tape[0] = (uint8_t)(i % 3 == 0 ? TAPE_SPD_HALF : i % 3 == 1 ? TAPE_SPD_2X : TAPE_SPD_1X);
            run(80 + (int)rnd(40));
        }
        st.tape[0] = TAPE_SPD_1X;
        h1_button(H1_BTN_PLAY, 1); h1_button(H1_BTN_PLAY, 0);
        run(200);
        h1_button(H1_BTN_REC, 1); run(700); h1_button(H1_BTN_REC, 0); run(300);   /* hold REC: the track cleared */
        snprintf(name, sizeof name, "WIRE %d", a);
        section(name);
        h1_encoder(H1_ENC_ALGO, 1);
        run(20);
    }
}

int main(int argc, char **argv)
{
    int before = argc > 1 && !strcmp(argv[1], "before"), f, bad = 0;
    static const int FXS[4] = {FX_DELAY, FX_SHIFT, FX_CHORUS, FX_TANK};
    flush_denormals();
    fx_test_no_sleep(before);
    wire_test_whole_row(before);
    h1_init(44100);
    h = 1469598103934665603ull;
    run(10);
    effects();
    wire();
    if (!before)
        for (f = 0; f < 4; f++)
            if (fx_test_slept(FXS[f]) == 0) {
                fprintf(stderr, "FAIL sleep: effect %d never slept, so its comparison proves nothing\n", FXS[f]);
                bad = 1;
            }
    return bad;
}
