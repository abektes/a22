/* a22's own features, on the core alone. Run by tests/run_tests.sh.
 *   1) two cores sound the same as one, to the bit, on all 10 engines x 8 algorithms (the second core a
 *      real thread), and every one of those 80 makes a sound
 *   1b) MONO is mono: overlapping keys stay one voice, and letting go of the first keeps the second
 *   2) the tape with knob automation, Cut & Paste and a snapshot, saved and loaded back from odd addresses
 *      (the AC7911B faults on a misaligned word), saved again the same to the byte; garbage is refused
 *   3) snapshots: SAVE + a white key keeps the patch, and brings it back
 *   4) every algorithm has its own knobs
 *   5) varispeed: the tape at 2x plays twice the notes in the same time, at 0.5x half, REV plays too
 *   6) Riff records a sequence and plays it transposed to the held key; Shake's balls play their note
 *   7) 800 frames of random panel input, every sample in range (with ASan/UBSan in run_tests.sh) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include "../core/internal.h"
#include "../core/h1.h"

static uint32_t fnv(uint32_t h, const int32_t *buf, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        h ^= (uint32_t)buf[i];
        h *= 16777619u;
    }
    return h;
}

/* Real pthread worker simulating the AC7911B's second core */
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv_go = PTHREAD_COND_INITIALIZER, cv_done = PTHREAD_COND_INITIALIZER;
static void (*w_fn)(uint32_t);
static uint32_t w_arg;
static int w_busy, w_stop;

static void *worker(void *p)
{
    (void)p;
    pthread_mutex_lock(&mu);
    for (;;) {
        while (!w_busy && !w_stop)
            pthread_cond_wait(&cv_go, &mu);
        if (w_stop) break;
        pthread_mutex_unlock(&mu);
        w_fn(w_arg);
        pthread_mutex_lock(&mu);
        w_busy = 0;
        pthread_cond_signal(&cv_done);
    }
    pthread_mutex_unlock(&mu);
    return 0;
}

static int c2_run(void (*fn)(uint32_t), uint32_t arg)
{
    pthread_mutex_lock(&mu);
    w_fn = fn; w_arg = arg; w_busy = 1;
    pthread_cond_signal(&cv_go);
    pthread_mutex_unlock(&mu);
    return 1;
}

static int c2_wait(void)
{
    pthread_mutex_lock(&mu);
    while (w_busy)
        pthread_cond_wait(&cv_done, &mu);
    pthread_mutex_unlock(&mu);
    return 0;
}

static uint32_t run_audio_hash(int two_cores)
{
    static int32_t lr[256 * 2];
    uint32_t h = 2166136261u;
    int inst, algo, b;
    h1_init(44100);
    if (two_cores) h1_two_cores(c2_run, c2_wait);
    else           h1_two_cores(0, 0);
    for (inst = 0; inst < h1_instrument_count(); inst++) {
        h1_control_set(H1_ENC_PRESET, inst);
        for (algo = 0; algo < 8; algo++) {
            h1_control_set(H1_ENC_ALGO, algo);
            h1_key(7, 1); h1_key(11, 1); h1_key(14, 1); h1_key(19, 1);
            {
                int32_t pk = 0;
                int j;
                for (b = 0; b < 6; b++) {
                    h1_render(lr, 256);
                    h = fnv(h, lr, 512);
                    for (j = 0; j < 512; j++)
                        if (lr[j] > pk || -lr[j] > pk) pk = lr[j] < 0 ? -lr[j] : lr[j];
                }
                if (pk < 1000)
                    printf("SILENT: engine %d algorithm %d (peak %d)\n", inst, algo, pk);
                assert(pk >= 1000);              /* every one of the 80 makes a sound */
            }
            h1_key(7, 0); h1_key(11, 0); h1_key(14, 0); h1_key(19, 0);
            for (b = 0; b < 3; b++) {
                h1_render(lr, 256);
                h = fnv(h, lr, 512);
            }
        }
    }
    return h;
}

/* ---- for sections 3 to 6: the panel as a hand works it, what goes out over MIDI counted ---- */
static uint32_t t_ms;
static int mo_on[128], mo_ons;
static void mo_fn(uint8_t s, uint8_t a, uint8_t b)
{
    if ((s & 0xF0) == 0x90 && b) {
        mo_on[a & 127]++;
        mo_ons++;
    }
}
static void mo_clear(void) { memset(mo_on, 0, sizeof mo_on); mo_ons = 0; }
static void go(int msec)
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        if (i % 3 == 0)
            h1_frame(t_ms += 17);
    }
}
static void tap(int b) { h1_button(b, 1); go(40); h1_button(b, 0); go(40); }
static void note(int key, int msec) { h1_key(key, 1); go(msec); h1_key(key, 0); go(40); }
static void fresh(void)
{
    int k;
    for (k = 0; k < H1_NKEYS; k++)               /* (ui.c keeps a key's down through h1_init) */
        h1_key(k, 0);
    h1_init(44100);
    h1_two_cores(0, 0);
    h1_midi_out(mo_fn);
    mo_clear();
    go(100);
}
static int loop_state(void) { SeqInfo si; eng_seq_info(&si); return si.loop_state; }

/* a take of 8 notes over the first bar of a 1-bar tape, then the tape stopped */
static void take_bar(void)
{
    int i;
    st.seq[1] = 0;                               /* BARS 1 */
    tap(H1_BTN_REC);
    for (i = 0; i < 8; i++)
        note(i, 4 * 60000 / st.glo[0] / 8 - 40);
    tap(H1_BTN_REC);
    tap(H1_BTN_PLAY);                            /* stop */
    go(200);
}
static int tape_notes_at(int speed, int msec)    /* note-ons the tape plays in msec at a speed */
{
    st.tape[0] = (uint8_t)speed;
    mo_clear();
    tap(H1_BTN_PLAY);
    go(msec);
    tap(H1_BTN_PLAY);
    go(200);
    return mo_ons;
}

static void sections_3_to_6(void)
{
    /* 3) snapshots */
    fresh();
    h1_control_set(H1_ENC_PRESET, 4);
    h1_control_set(H1_ENC_ALGO, 5);
    h1_button(H1_BTN_SAVE, 1); go(30); h1_key(2, 1); go(30); h1_key(2, 0); h1_button(H1_BTN_SAVE, 0); go(60);
    assert(st.snap[1].used);
    h1_control_set(H1_ENC_PRESET, 8);
    h1_control_set(H1_ENC_ALGO, 1);
    go(60);
    assert(h1_instrument() == 8);
    h1_button(H1_BTN_SAVE, 1); go(30); h1_key(2, 1); go(30); h1_key(2, 0); h1_button(H1_BTN_SAVE, 0); go(200);
    assert(h1_instrument() == 4 && h1_algorithm() == 5);
    printf("PASS: snapshot 2 kept SHAPER algorithm 6 and brought it back\n");

    /* 4) every algorithm its own knobs */
    fresh();
    h1_control_set(H1_ENC_ALGO, 0);
    h1_control_set(H1_ENC_K1, 10);
    h1_control_set(H1_ENC_ALGO, 1);
    h1_control_set(H1_ENC_K1, 90);
    h1_control_set(H1_ENC_ALGO, 0);
    assert(KNOBS_NOW(st.inst)[0] == 10);
    h1_control_set(H1_ENC_ALGO, 1);
    assert(KNOBS_NOW(st.inst)[0] == 90);
    assert(KNOBS(st.inst, 0)[0] == 10);
    printf("PASS: each algorithm keeps its own knobs\n");

    /* 5) varispeed */
    {
        int bar, one, two, half, rev;
        fresh();
        take_bar();
        assert(loop_state() == LOOP_IDLE);
        bar = 4 * 60000 / st.glo[0];
        one = tape_notes_at(TAPE_SPD_1X, 4 * bar);
        two = tape_notes_at(TAPE_SPD_2X, 4 * bar);
        half = tape_notes_at(TAPE_SPD_HALF, 4 * bar);
        rev = tape_notes_at(TAPE_SPD_REV, 4 * bar);
        printf("      varispeed over 4 bars: 1x %d notes, 2x %d, 0.5x %d, REV %d\n", one, two, half, rev);
        assert(one >= 28 && one <= 36);         /* 8 a pass, four passes (give or take the edges) */
        assert(two >= 2 * one - 6 && two <= 2 * one + 6);
        assert(half >= one / 2 - 4 && half <= one / 2 + 4);
        assert(rev >= one - 6 && rev <= one + 6);
        printf("PASS: varispeed: 2x twice the notes, 0.5x half, REV as many\n");
    }

    /* 6) Riff and Shake */
    fresh();
    st.ip[st.inst].arp[0] = ARP_RIFF;
    tap(H1_BTN_ARP);                             /* the ARP page: REC records the Riff sequence */
    assert(h1_page() == PG_ARP);
    tap(H1_BTN_REC);
    note(0, 80); note(4, 80); note(7, 80);       /* 53, 57, 60 */
    tap(H1_BTN_REC);
    assert(st.riff_len == 3 && st.riff_root == 53);
    tap(H1_BTN_HOME);
    mo_clear();
    h1_key(12, 1);                               /* 65: the sequence an octave up */
    go(3000);
    h1_key(12, 0);
    go(300);
    assert(mo_on[65] > 0 && mo_on[69] > 0 && mo_on[72] > 0 && mo_on[53] == 0);
    printf("PASS: Riff recorded 3 notes and played them transposed (65:%d 69:%d 72:%d)\n", mo_on[65], mo_on[69], mo_on[72]);

    fresh();
    st.ip[st.inst].arp[0] = ARP_SHAKE;
    mo_clear();
    h1_key(7, 1);                                /* a ball for 60 */
    go(80);
    h1_key(7, 0);
    go(4000);
    assert(mo_on[60] >= 2);
    printf("PASS: Shake's ball played its note %d times as it bounced\n", mo_on[60]);
    h1_midi_out(0);
}

int main(void)
{
    static int32_t lr[256 * 2];
    pthread_t th;
    uint32_t h1_one, h1_two;

    /* 1) Dual-core bit-exactness across all 10 engines x 8 algorithms */
    pthread_create(&th, 0, worker, 0);
    h1_one = run_audio_hash(0);
    h1_two = run_audio_hash(1);
    pthread_mutex_lock(&mu); w_stop = 1; pthread_cond_signal(&cv_go); pthread_mutex_unlock(&mu);
    pthread_join(th, 0);
    printf("dual-core hash: 1-core=0x%08X  2-core=0x%08X (%d engines x 8 = %d presets)\n",
           h1_one, h1_two, h1_instrument_count(), h1_instrument_count() * 8);
    assert(h1_one == h1_two);
    assert(h1_instrument_count() == 10);
    printf("PASS: dual-core bit-exactness across all 80 presets!\n");

    /* 1b) MONO mono-legato verification: overlapping keys stay 1 active voice & glide cleanly */
    {
        h1_init(44100);
        h1_control_set(H1_ENC_PRESET, 7); /* MONO */
        h1_key(7, 1);
        h1_render(lr, 256);
        assert(h1_voices_active() == 1u);
        h1_key(14, 1);
        h1_render(lr, 256);
        assert(h1_voices_active() == 1u); /* mono priority: still 1 voice! */
        h1_key(7, 0);
        h1_render(lr, 256);
        assert(h1_voices_active() == 1u); /* releasing earlier note does not cut held note! */
        h1_key(14, 0);
        printf("PASS: MONO mono-legato voice priority verified!\n");
    }

    /* 2) Odd-address state & loop save/load + 4-track tape + Cut/Paste + Automation */
    {
        uint8_t raw_state[4096], raw_loop[H1_LOOP_MAX + 16];
        uint32_t sz = 0, lsz = 0, ms = 0;
        const void *p;
        int i;
        h1_init(44100);
        h1_two_cores(0, 0);
        /* arm tape, record notes + K1/K2 motion automation */
        h1_button(H1_BTN_REC, 1); h1_button(H1_BTN_REC, 0);
        h1_render(lr, 256);
        h1_key(7, 1);
        for (i = 0; i < 8; i++) {
            h1_encoder(H1_ENC_K1, 4);
            h1_encoder(H1_ENC_K4, -3);
            h1_frame(ms += 16);
            h1_render(lr, 256);
        }
        h1_key(7, 0);
        h1_render(lr, 256);
        /* stop tape */
        h1_button(H1_BTN_PLAY, 1); h1_button(H1_BTN_PLAY, 0);
        h1_render(lr, 256);
        /* save snapshot 1 via SAVE + white key 0 */
        h1_button(H1_BTN_SAVE, 1); h1_key(0, 1); h1_key(0, 0); h1_button(H1_BTN_SAVE, 0);
        h1_render(lr, 256);
        /* test Cut & Paste via EDIT+REC then EDIT+PLAY */
        h1_button(H1_BTN_EDIT, 1); h1_button(H1_BTN_REC, 1); h1_button(H1_BTN_REC, 0); h1_button(H1_BTN_EDIT, 0);
        h1_render(lr, 256);
        h1_button(H1_BTN_EDIT, 1); h1_button(H1_BTN_PLAY, 1); h1_button(H1_BTN_PLAY, 0); h1_button(H1_BTN_EDIT, 0);
        h1_render(lr, 256);

        /* copy state and loop to ODD-ALIGNED addresses (+1) and reload! */
        p = h1_state(&sz);
        assert(sz <= 3840u);
        memcpy(raw_state + 1, p, sz);
        p = h1_loop(&lsz);
        assert(lsz >= 12u && lsz <= H1_LOOP_MAX);
        memcpy(raw_loop + 1, p, lsz);

        h1_init(44100);
        assert(h1_state_load(raw_state + 1, sz) == 0);
        assert(h1_loop_load(raw_loop + 1, lsz) == 0);
        {                                        /* saved again: the same to the byte */
            uint32_t sz2, lsz2;
            const void *q = h1_state(&sz2);
            assert(sz2 == sz && memcmp(q, raw_state + 1, sz) == 0);
            q = h1_loop(&lsz2);
            assert(lsz2 == lsz && memcmp(q, raw_loop + 1, lsz) == 0);
        }
        /* garbage must be cleanly rejected */
        raw_state[1] ^= 0xFF;
        assert(h1_state_load(raw_state + 1, sz) != 0);
        printf("PASS: state=%u B (<=3840), loop=%u B (odd-aligned load + cut/paste/auto/snap)\n", sz, lsz);
    }

    sections_3_to_6();

    /* 7) Stress fuzzing: 800 frames of random buttons, encoders, keys, FX, Shake, Riff, Tape tricks */
    {
        uint32_t rng = 0xA22F0221u, ms = 0;
        int f, i;
        h1_init(44100);
        for (f = 0; f < 800; f++) {
            uint32_t r;
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; r = rng;
            if ((r & 3u) == 0) h1_key((int)(r >> 8) % H1_NKEYS, (int)(r >> 16) & 1);
            if ((r & 7u) == 1) h1_button((int)(r >> 8) % H1_NBTN, (int)(r >> 16) & 1);
            if ((r & 3u) == 2) h1_encoder((int)(r >> 8) % H1_NENC, ((int)(r >> 16) & 7) - 3);
            if ((r & 15u) == 3) h1_master((int)(r >> 8) % 1001);
            h1_frame(ms += 16);
            h1_present(0);
            h1_render(lr, 256);
            for (i = 0; i < 512; i++) {
                assert(lr[i] >= -8388608 && lr[i] <= 8388607);
            }
        }
        printf("PASS: 800-frame ASan+UBSan stress fuzz (0 crashes, 0 UB, 0 out-of-range samples)\n");
    }
    return 0;
}
