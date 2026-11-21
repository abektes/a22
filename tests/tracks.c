/* a22's tracks, as a hand moves between them: each of T1..T4 keeps the engine and algorithm it was given when
 * EDIT + PRESETS goes to another and back; a key held while the track, the engine or the sequencer mode changes
 * still lets go of its note, on the FM-1 and over MIDI out; the tape's knob automation and varispeed reach the recorded part on the track in
 * hand too; the white keys show all of a 64-step pattern, and the tape. Run by tests/run_tests.sh. */
#include <stdio.h>
#include <string.h>
#include "../core/internal.h"
#include "../core/h1.h"

static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

static uint32_t ms;
static void go(int msec)
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        if (i % 4 == 0)
            h1_frame(ms += 23);
    }
}

static void fresh(void)
{
    int k;
    for (k = 0; k < H1_NKEYS; k++)
        h1_key(k, 0);
    h1_init(44100);
    h1_two_cores(0, 0);
    ms = 0;
    go(50);
}

static void track(int d)                         /* EDIT held, PRESETS turned, EDIT let go */
{
    h1_button(H1_BTN_EDIT, 1);
    h1_encoder(H1_ENC_PRESET, d);
    h1_button(H1_BTN_EDIT, 0);
}

static void arp_mode(int d);

/* MIDI out, counted: notes the FM-1 sent on that it has not sent off */
static int midi_open[128];
static void midi_count(uint8_t status, uint8_t d1, uint8_t d2)
{
    if ((status & 0xF0) == 0x90 && d2) midi_open[d1 & 127]++;
    else if ((status & 0xF0) == 0x80 || (status & 0xF0) == 0x90) midi_open[d1 & 127]--;
}
static int midi_hanging(void)
{
    int i, n = 0;
    for (i = 0; i < 128; i++)
        if (midi_open[i] > 0) n++;
    return n;
}

/* a key pressed with the sequencer OFF, the mode turned (d) while it is held, then let go (jakehandy/h1#56) */
static void midi_note_off_through_mode_change(int d, const char *what)
{
    fresh();
    memset(midi_open, 0, sizeof midi_open);
    h1_midi_out(midi_count);
    h1_control_set(H1_ENC_PRESET, 0);
    go(30);
    h1_key(7, 1);
    go(100);
    CHECK(midi_hanging() == 1, "a key sends its note on over MIDI out");
    arp_mode(d);
    go(30);
    h1_key(7, 0);
    go(2000);
    CHECK(midi_hanging() == 0, what);
    CHECK(h1_voices_active() == 0u, "and its own voice lets go");
    arp_mode(-d);
    h1_midi_out(0);
}

/* SHAKE: keys dropped as balls, let go; once the balls have bounced out, every note they played has ended,
 * on the FM-1 and over MIDI out */
static void shake_balls_end_their_notes(void)
{
    int k;
    fresh();
    memset(midi_open, 0, sizeof midi_open);
    h1_midi_out(midi_count);
    h1_control_set(H1_ENC_PRESET, 0);
    go(30);
    arp_mode(2);                                 /* OFF, RIFF, SHAKE */
    go(30);
    CHECK(st.ip[st.inst].arp[0] == ARP_SHAKE, "EDIT + ALGORITHM turned the mode to SHAKE");
    for (k = 0; k < 6; k++) {
        h1_key(4 + k * 2, 1);
        go(60);
        h1_key(4 + k * 2, 0);
        go(60);
    }
    go(30000);
    CHECK(midi_hanging() == 0, "SHAKE's balls send every note off over MIDI out when they bounce out");
    CHECK(h1_voices_active() == 0u, "SHAKE's balls let go of every note when they bounce out");
    arp_mode(-2);
    h1_midi_out(0);
}

static void arp_mode(int d)                      /* EDIT held, ALGORITHM turned */
{
    h1_button(H1_BTN_EDIT, 1);
    h1_encoder(H1_ENC_ALGO, d);
    h1_button(H1_BTN_EDIT, 0);
}

static void tracks_keep_their_engines(void)
{
    static const int want_inst[4] = {0, 1, 3, 8}, want_algo[4] = {2, 5, 1, 4};
    int t, round;
    fresh();
    for (t = 0; t < 4; t++) {
        if (t) { track(1); go(30); }
        h1_control_set(H1_ENC_PRESET, want_inst[t]);
        go(30);
        h1_control_set(H1_ENC_ALGO, want_algo[t]);
        go(30);
    }
    for (round = 0; round < 2; round++)
        for (t = 0; t < 4; t++) {
            char what[80];
            track(1);
            go(30);
            snprintf(what, sizeof what, "round %d: T%d keeps its engine", round, (int)st.track + 1);
            CHECK(h1_instrument() == want_inst[st.track], what);
            snprintf(what, sizeof what, "round %d: T%d keeps its algorithm", round, (int)st.track + 1);
            CHECK(h1_algorithm() == want_algo[st.track], what);
        }
}

static void held_keys_let_go(void)
{
    /* a key held on T1, the track changed, the key let go: its note ends */
    fresh();
    h1_control_set(H1_ENC_PRESET, 0);
    h1_control_set(H1_ENC_ALGO, 3);              /* FM-4 PAD: it sustains while held */
    go(30);
    h1_key(7, 1);
    go(100);
    CHECK(h1_voices_active() == 1u, "a held key sounds");
    track(1);
    go(30);
    h1_key(7, 0);
    go(4000);
    CHECK(h1_voices_active() == 0u, "a key held through a track change lets go of its note");

    /* the same through an engine change */
    fresh();
    h1_control_set(H1_ENC_PRESET, 0);
    h1_control_set(H1_ENC_ALGO, 3);
    go(30);
    h1_key(7, 1);
    go(100);
    h1_control_set(H1_ENC_PRESET, 2);
    go(30);
    h1_key(7, 0);
    go(4000);
    CHECK(h1_voices_active() == 0u, "a key held through an engine change lets go of its note");

    /* and through a sequencer mode change (OFF to UP) */
    fresh();
    h1_control_set(H1_ENC_PRESET, 0);
    h1_control_set(H1_ENC_ALGO, 3);
    go(30);
    h1_key(7, 1);
    go(100);
    arp_mode(3);                                 /* OFF, RIFF, SHAKE, UP */
    go(30);
    CHECK(st.ip[st.inst].arp[0] == ARP_UP, "EDIT + ALGORITHM turned the mode to UP");
    h1_key(7, 0);
    go(4000);
    CHECK(h1_voices_active() == 0u, "a key held through a sequencer mode change lets go of its note");
    arp_mode(-3);
}

static void leds_show_the_pattern(void)
{
    uint32_t m = 0;
    int i;
    fresh();
    st.seq[0] = SEQ_STEP;
    for (i = 0; i < NSTEP; i++)
        st.steps[i][0] = 0;
    st.steps[63][0] = 61;                        /* the last step of a 64-step pattern */
    go(30);
    CHECK(eng_seq_leds(&m, 1, 0) == 1, "the white keys show the step pattern with its page open");
    CHECK((m >> 26) & 1u, "a note on step 64 lights the last white key");

    /* the tape: a take recorded and playing shows on the white keys */
    fresh();
    h1_button(H1_BTN_REC, 1); h1_button(H1_BTN_REC, 0);
    go(50);
    h1_key(7, 1); go(120); h1_key(7, 0); go(200);
    h1_key(9, 1); go(120); h1_key(9, 0); go(200);
    h1_button(H1_BTN_REC, 1); h1_button(H1_BTN_REC, 0);
    go(100);
    m = 0;
    CHECK(eng_seq_leds(&m, 0, 0) == 1, "the white keys show the tape while it plays");
    CHECK(m != 0u, "the tape's notes light white keys");
}

/* zero crossings of the left channel over msec: a note an octave up crosses twice as often */
static int crossings(int msec)
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i, j, n = 0;
    int32_t last = 0;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        for (j = 0; j < 512; j += 2) {
            if ((out[j] >= 0) != (last >= 0)) n++;
            last = out[j];
        }
        if (i % 4 == 0)
            h1_frame(ms += 23);
    }
    return n;
}

/* the tape's varispeed with its pitch link: the recorded part on the track in hand is shifted too */
static int playback_crossings(int speed)
{
    fresh();
    h1_control_set(H1_ENC_PRESET, 0);
    h1_control_set(H1_ENC_ALGO, 6);              /* FM-4 ORGAN: steady while held */
    st.fx_on = 0;
    go(30);
    h1_button(H1_BTN_REC, 1); h1_button(H1_BTN_REC, 0);
    go(30);
    h1_key(7, 1); go(1500); h1_key(7, 0);
    go(100);
    h1_button(H1_BTN_REC, 1); h1_button(H1_BTN_REC, 0);   /* the take closes and plays on */
    h1_button(H1_BTN_PLAY, 1); h1_button(H1_BTN_PLAY, 0); /* stop */
    go(1000);                                             /* (its release dies away) */
    st.tape[0] = (uint8_t)speed;
    st.seq[7] = 1;                                        /* pitch follows speed */
    h1_button(H1_BTN_PLAY, 1); h1_button(H1_BTN_PLAY, 0); /* from the start */
    go(150);
    return crossings(300);
}

static void varispeed_reaches_the_track_in_hand(void)
{
    int one = playback_crossings(TAPE_SPD_1X), two = playback_crossings(TAPE_SPD_2X);
    char what[96];
    snprintf(what, sizeof what, "2X with pitch link plays the track in hand an octave up (%d vs %d crossings)", two, one);
    CHECK(one > 20 && two > one * 17 / 10 && two < one * 23 / 10, what);
}

/* the PATTERN page's lanes show each track's steps (they showed the tape's notes, so only a track with a tape
 * recording ever lit) */
static void pattern_lanes_show_each_track(void)
{
    SeqInfo si;
    int t, c, n[4] = {0, 0, 0, 0};
    fresh();
    h1_button(H1_BTN_SEQ, 1); go(800); h1_button(H1_BTN_SEQ, 0); go(50);   /* hold SEQ: STEP */
    CHECK(st.seq[0] == SEQ_STEP, "holding SEQ swaps the tape for the step sequencer");
    for (t = 0; t < 3; t++) {
        if (t) { track(1); go(30); }
        h1_button(H1_BTN_REC, 1); h1_button(H1_BTN_REC, 0); go(30);
        h1_key(7 + 2 * t, 1); go(60); h1_key(7 + 2 * t, 0); go(60);
        h1_button(H1_BTN_REC, 1); h1_button(H1_BTN_REC, 0); go(30);
    }
    eng_seq_info(&si);
    for (t = 0; t < 4; t++)
        for (c = 0; c < 32; c++)
            n[t] += si.trk_dens[t][c];
    CHECK(n[0] > 0 && n[1] > 0 && n[2] > 0, "steps written on T1, T2 and T3 each show in their own lane");
    CHECK(n[3] == 0, "T4, with no steps, shows none");
}

int main(void)
{
    tracks_keep_their_engines();
    held_keys_let_go();
    leds_show_the_pattern();
    varispeed_reaches_the_track_in_hand();
    pattern_lanes_show_each_track();
    midi_note_off_through_mode_change(3, "a key held from OFF into UP sends its note off over MIDI out");
    midi_note_off_through_mode_change(2, "a key held from OFF into SHAKE sends its note off over MIDI out");
    shake_balls_end_their_notes();
    printf("tracks: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
