/* MIDI out as a DAW records it, on the core alone. Run by tests/run_tests.sh.
 *
 *   channels     each track's notes on its own channel (T1 on 1 .. T4 on 4): the tape's, the step sequencer's,
 *                and the keys' and the arpeggiator's on the track in hand's; a note-off on the channel its
 *                note-on went on, a track change in between or not
 *   CCs          a knob on the track in hand's channel, the tape's automation on its track's, HUSH's CC 123
 *                on all four
 */
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
        if (i % 3 == 0)
            h1_frame(ms += 17);
    }
}
static void tap(int b) { h1_button(b, 1); go(40); h1_button(b, 0); go(40); }
static void note(int key, int msec) { h1_key(key, 1); go(msec); h1_key(key, 0); go(60); }
static void track(int d)                         /* EDIT held, PRESETS turned, EDIT let go */
{
    h1_button(H1_BTN_EDIT, 1);
    h1_encoder(H1_ENC_PRESET, d);
    h1_button(H1_BTN_EDIT, 0);
    go(40);
}

/* ---- what went out ---- */
static uint8_t mo[4096][3];
static int nmo;
static void mo_fn(uint8_t s, uint8_t a, uint8_t b)
{
    if (nmo < 4096) {
        mo[nmo][0] = s;
        mo[nmo][1] = a;
        mo[nmo][2] = b;
        nmo++;
    }
}
static int mo_n(uint8_t s, int d1)               /* (d1 < 0: any) */
{
    int i, n = 0;
    for (i = 0; i < nmo; i++)
        if (mo[i][0] == s && (d1 < 0 || mo[i][1] == d1))
            n++;
    return n;
}
static int balanced(void)                        /* every channel's every note: no on left without its off (a
                                                  * spare off is harmless, and the arpeggiator sends one today) */
{
    static int open[16][128];
    int i, c, k;
    memset(open, 0, sizeof open);
    for (i = 0; i < nmo; i++) {
        int t = mo[i][0] & 0xF0, ch = mo[i][0] & 15, n = mo[i][1] & 127;
        if (t == 0x90 && mo[i][2]) open[ch][n]++;
        else if (t == 0x80 || t == 0x90) open[ch][n]--;
    }
    for (c = 0; c < 16; c++)
        for (k = 0; k < 128; k++)
            if (open[c][k] > 0) {
                printf("  channel %d note %d: %d on without its off\n", c + 1, k, open[c][k]);
                return 0;
            }
    return 1;
}
static int loop_state(void)
{
    SeqInfo si;
    eng_seq_info(&si);
    return si.loop_state;
}
static void fresh(void)
{
    int k;
    for (k = 0; k < H1_NKEYS; k++)
        h1_key(k, 0);
    h1_init(44100);
    ms = 0;
    nmo = 0;
    h1_midi_out(mo_fn);
    go(100);
}

static void keys_on_t2(void)
{
    fresh();
    track(+1);
    nmo = 0;
    note(7, 100);
    CHECK(mo_n(0x91, 60) == 1 && mo_n(0x81, 60) == 1, "a key on T2 goes out on channel 2");
    CHECK(mo_n(0x90, -1) == 0, "and nothing on channel 1");
}

static void key_held_across_track_change(void)
{
    fresh();
    h1_key(7, 1);
    go(100);
    track(+1);
    go(100);
    h1_key(7, 0);
    go(100);
    CHECK(mo_n(0x90, 60) == 1 && mo_n(0x80, 60) == 1, "a key held across a track change ends on its own channel");
    CHECK(mo_n(0x81, 60) == 0, "not on the new one");
}

static void arp_across_track_change(void)
{
    fresh();
    st.ip[st.inst].arp[0] = ARP_UP;
    h1_key(7, 1);
    h1_key(11, 1);
    go(600);
    track(+1);
    go(600);
    h1_key(7, 0);
    h1_key(11, 0);
    go(500);
    CHECK(mo_n(0x90, -1) > 0, "the arpeggiator played on channel 1");
    CHECK(balanced(), "the arpeggiator's notes end on the channel they began on");
}

static void tape_tracks_on_their_channels(void)
{
    fresh();
    tap(H1_BTN_REC);                             /* a take on T1 */
    note(7, 150);
    note(9, 150);
    note(11, 150);
    tap(H1_BTN_REC);
    CHECK(loop_state() == LOOP_PLAY, "T1's take plays");
    track(+1);
    tap(H1_BTN_REC);                             /* overdub on T2, and its K1 */
    note(12, 150);
    h1_encoder(H1_ENC_K1, 5);
    note(14, 150);
    note(16, 150);
    tap(H1_BTN_REC);
    nmo = 0;
    go(9000);                                    /* a pass of the 4-bar tape */
    go(37);
    tap(H1_BTN_PLAY);                            /* stopped wherever it was */
    go(200);
    CHECK(mo_n(0x90, -1) > 0 && mo_n(0x91, -1) > 0, "the tape plays T1 on channel 1 and T2 on channel 2");
    CHECK(balanced(), "every tape note ends on its own channel, stopped mid-note too");
    CHECK(mo_n(0xB1, 20) > 0 && mo_n(0xB0, 20) == 0, "T2's recorded knob goes out as CC 20 on channel 2");
}

static void step_notes_on_their_track(void)
{
    fresh();
    st.seq[0] = SEQ_STEP;
    memset(st.steps, 0, sizeof st.steps);
    st.steps[0][0] = 61;                         /* note 60 on T3, step 1 */
    st.step_trk[0][0] = 2;
    tap(H1_BTN_PLAY);
    go(3000);
    tap(H1_BTN_PLAY);
    go(200);
    CHECK(mo_n(0x92, 60) > 0, "a step on T3 goes out on channel 3");
    CHECK(balanced(), "with its offs");
}

static void knob_cc_on_track_channel(void)
{
    fresh();
    track(+2);
    nmo = 0;
    h1_encoder(H1_ENC_K1, 5);
    go(100);
    CHECK(mo_n(0xB2, 20) > 0 && mo_n(0xB0, 20) == 0, "a knob on T3 goes out as CC 20 on channel 3");
}

static void hush_on_four_channels(void)
{
    fresh();
    h1_button(H1_BTN_HOME, 1);
    go(1000);
    h1_button(H1_BTN_HOME, 0);
    go(100);
    CHECK(mo_n(0xB0, 123) == 1 && mo_n(0xB1, 123) == 1 && mo_n(0xB2, 123) == 1 && mo_n(0xB3, 123) == 1,
          "HUSH sends CC 123 on channels 1 to 4");
}

static void t1_unchanged(void)
{
    fresh();
    note(7, 100);
    CHECK(mo_n(0x90, 60) == 1 && mo_n(0x80, 60) == 1, "a key on T1 goes out on channel 1 as before");
}

int main(void)
{
    keys_on_t2();
    key_held_across_track_change();
    arp_across_track_change();
    tape_tracks_on_their_channels();
    step_notes_on_their_track();
    knob_cc_on_track_channel();
    hush_on_four_channels();
    t1_unchanged();
    h1_midi_out(0);
    printf("midiout: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
