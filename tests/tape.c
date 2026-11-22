/* The tape's edges: notes that hung or burst, found by a review before 0.4.0. Run by tests/run_tests.sh.
 *   snap      SNAP moves a note's off with its on: a short note played just before a grid line keeps its length
 *             (its off came before its on, and it sounded until the tape came round)
 *   mute      muting a track while its note sounds still lets the note end
 *   shorten   BARS turned down while the tape plays past the new end: no burst of every note
 *   rev       no recording in REV (a take in REV played back as held notes): REC says so
 *   lone off  a key held into a take and let go during it records nothing
 *   cut       CUT keeps a note begun before IN whole on the tape, and lets go of what it cut
 *   clear     clearing one track lets go of that track's notes alone
 *   first     a take's first note plays on the pass right after the take (it was skipped once) */
#include <stdio.h>
#include <string.h>
#include "../core/internal.h"
#include "../core/h1.h"

static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

static uint64_t smp;
static uint32_t ms;
static int ons, longest_ms;
static int gate_ms[NVOICE];

static void midi(uint8_t s, uint8_t a, uint8_t b) { (void)a; if ((s & 0xF0) == 0x90 && b) ons++; }
static double now(void) { return (double)smp * 1000.0 / 44100.0; }
static void until(double at_ms)                  /* render to a moment; the longest a tape voice stays gated */
{
    static int32_t out[512];
    int v;
    while (now() < at_ms) {
        h1_render(out, 256);
        smp += 256;
        if ((smp / 256) % 3 == 0)
            h1_frame(ms += 17);
        for (v = 0; v < NVOICE; v++) {
            if (voices[v].stage && voices[v].gate && voices[v].seq) {
                gate_ms[v] += 6;                 /* (256 frames: 5.8 ms) */
                if (gate_ms[v] > longest_ms) longest_ms = gate_ms[v];
            } else
                gate_ms[v] = 0;
        }
    }
}
static void tap(int b) { h1_button(b, 1); until(now() + 30); h1_button(b, 0); until(now() + 30); }
static void key(int k, double at, double len) { until(at); h1_key(k, 1); until(at + len); h1_key(k, 0); }
static void fresh(int bars_code)
{
    int k;
    for (k = 0; k < H1_NKEYS; k++) h1_key(k, 0);
    h1_init(44100);
    h1_two_cores(0, 0);
    h1_midi_out(midi);
    st.glo[0] = 120;                             /* (a bar: 2 s) */
    st.seq[1] = (uint8_t)bars_code;              /* 0: one bar */
    until(now() + 200);
}
static int tape_gated(void)
{
    int v, n = 0;
    for (v = 0; v < NVOICE; v++) n += voices[v].stage && voices[v].gate && voices[v].seq;
    return n;
}
static int trk_gated(int t)
{
    int v, n = 0;
    for (v = 0; v < NVOICE; v++) n += voices[v].stage && voices[v].gate && voices[v].seq && voices[v].trk == t;
    return n;
}
static int loop_state(void) { SeqInfo si; eng_seq_info(&si); return si.loop_state; }
static void reset_gates(void) { memset(gate_ms, 0, sizeof gate_ms); longest_ms = 0; }

int main(void)
{
    /* ---- first ---- */
    {
        double t0;
        fresh(0);
        tap(H1_BTN_REC);
        t0 = now() + 50;
        key(4, t0, 600);
        until(t0 + 2000 + 100);
        CHECK(tape_gated() == 1, "first: the take's first note sounds on the pass right after it");
    }

    /* ---- snap ---- */
    {
        double t0;
        int k;
        fresh(0);
        st.seq[3] = 1;                           /* SNAP 1/16: 125 ms at 120 BPM */
        tap(H1_BTN_REC);                         /* armed: the first key starts the take */
        t0 = now() + 50;
        key(0, t0, 100);
        for (k = 1; k < 15; k++)                 /* 30 ms notes, from just after a line to just before the next */
            key(k % 12, t0 + k * 125 + (k * 13) % 120, 30);
        until(t0 + 2000 + 20);                   /* the take ends as the loop comes round */
        CHECK(loop_state() == LOOP_PLAY, "snap: the take plays");
        reset_gates();
        until(t0 + 4000 + 20);
        CHECK(longest_ms < 200, "snap: no note snapped forward sounds past its length on playback");
        if (longest_ms >= 200) printf("     (the longest held %d ms)\n", longest_ms);
    }

    /* ---- mute ---- */
    {
        double t0;
        fresh(0);
        tap(H1_BTN_REC);
        t0 = now() + 50;
        key(4, t0, 600);
        until(t0 + 4000 + 100);                  /* (the second pass) */
        CHECK(tape_gated() == 1, "mute: the recorded note sounds on playback");
        st.trk[0].mute = 1;                      /* muted while it sounds */
        until(t0 + 4000 + 900);                  /* past its end */
        CHECK(tape_gated() == 0, "mute: muted while it sounds, it still ends");
        st.trk[0].mute = 0;
        until(t0 + 4000 + 950);
        CHECK(tape_gated() == 0, "mute: unmuted, nothing comes back before its next start");
    }

    /* ---- shorten ---- */
    {
        double t0;
        int bar, k;
        fresh(3);                                /* 8 bars, 16 s */
        tap(H1_BTN_REC);
        t0 = now() + 50;
        for (bar = 0; bar < 8; bar++)
            for (k = 0; k < 8; k++)
                key(k, t0 + bar * 2000 + k * 250, 100);
        until(t0 + 16000 + 20);
        until(t0 + 16000 + 5 * 2000 + 520);      /* playing, in bar 6 */
        ons = 0;
        st.seq[1] = 0;                           /* BARS to 1 */
        until(now() + 100);
        CHECK(ons <= 2, "shorten: BARS turned down past the playhead fires no burst of notes");
        if (ons > 2) printf("     (%d note-ons in 100 ms)\n", ons);
        reset_gates();
        until(now() + 2000);
        CHECK(longest_ms < 200, "shorten: and the one bar left plays as recorded");
    }

    /* ---- rev ---- */
    {
        double t0;
        fresh(0);
        st.tape[0] = TAPE_SPD_REV;
        tap(H1_BTN_REC);
        CHECK(loop_state() == LOOP_EMPTY && !strcmp(h1_toast_now(), "NO REC IN REV"), "rev: REC in REV records nothing, and says so");
        st.tape[0] = TAPE_SPD_1X;
        tap(H1_BTN_REC);
        t0 = now() + 50;
        key(2, t0, 200);
        until(t0 + 2000 + 20);
        st.tape[0] = TAPE_SPD_REV;
        tap(H1_BTN_REC);
        CHECK(loop_state() == LOOP_PLAY, "rev: playing in REV, REC does not overdub");
        st.tape[0] = TAPE_SPD_1X;
    }

    /* ---- lone off ---- */
    {
        double t0;
        uint32_t plain, held;
        fresh(0);
        tap(H1_BTN_REC);
        t0 = now() + 50;
        key(9, t0, 100);
        until(t0 + 2000 + 20);
        (void)h1_loop(&plain);
        fresh(0);
        h1_key(5, 1);                            /* held before the take */
        until(now() + 50);
        tap(H1_BTN_REC);
        t0 = now() + 50;
        until(t0);
        h1_key(9, 1);                            /* starts the take */
        until(t0 + 50);
        h1_key(5, 0);                            /* let go during it */
        until(t0 + 100);
        h1_key(9, 0);
        until(t0 + 2000 + 20);
        (void)h1_loop(&held);
        CHECK(held == plain, "lone off: a key held into the take and let go in it records nothing");
    }

    /* ---- cut ---- */
    {
        double t0;
        fresh(1);                                /* 2 bars, 4 s */
        tap(H1_BTN_REC);
        t0 = now() + 50;
        key(3, t0 + 1500, 1000);                 /* bar 1 into bar 2 */
        key(7, t0 + 3000, 200);                  /* in bar 2 */
        until(t0 + 4000 + 20);
        st.tape[1] = 1;                          /* IN at bar 2 */
        until(t0 + 4000 + 1000);                 /* (it plays from IN: let it settle) */
        st.tape[1] = 0;
        until(t0 + 8000 + 1600);                 /* the long note sounding, in bar 1 */
        st.tape[1] = 1;
        eng_post(EV_CUT, 0);
        st.tape[1] = 0;
        until(now() + 40);
        reset_gates();
        until(now() + 8000);
        CHECK(longest_ms < 1200, "cut: a note begun before IN still ends (its end stays on the tape)");
        if (longest_ms >= 1200) printf("     (held %d ms)\n", longest_ms);
    }

    /* ---- clear ---- */
    {
        double t0;
        fresh(0);
        tap(H1_BTN_REC);
        t0 = now() + 50;
        key(0, t0, 1500);
        until(t0 + 2000 + 20);
        h1_button(H1_BTN_EDIT, 1); h1_encoder(H1_ENC_PRESET, 1); h1_button(H1_BTN_EDIT, 0);   /* to T2 */
        until(t0 + 4000 - 200);
        tap(H1_BTN_REC);                         /* overdub on T2 */
        key(7, t0 + 4000 + 50, 1500);
        until(t0 + 4000 + 600);
        tap(H1_BTN_REC);
        until(t0 + 6000 + 300);                  /* both play */
        CHECK(trk_gated(0) == 1 && trk_gated(1) == 1, "clear: T1 and T2 both sound");
        eng_post(EV_CLEAR, 2);                   /* T2 cleared */
        until(now() + 40);
        CHECK(trk_gated(0) == 1 && trk_gated(1) == 0, "clear: clearing T2 lets go of T2's note alone");
    }

    printf("tape: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
