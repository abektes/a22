/* The tape's room and length. From h1 (user feedback for its 0.3.0: a loop that silently dropped the
 * later layers' notes once 384 events were taken): 2048 events, and a full tape says so (TAPE FULL) and
 * never keeps a note-on without its note-off. a22's tape is 1 to 32 bars. And a22's own: a tape recorded
 * long, then shortened, keeps what lies past its new end through a save and a load (it used to drop it).
 *
 * Run by tests/run_tests.sh: the core alone, its panel worked as a hand would, and what the loop plays
 * counted on MIDI out. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/internal.h"
#include "../core/h1.h"

static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

static uint64_t smp;                             /* samples rendered */
static uint32_t ms;
static int ons, offs, counting;
static uint8_t heard[128];

static void midi(uint8_t status, uint8_t d1, uint8_t d2)
{
    if (!counting)
        return;
    if ((status & 0xF0) == 0x90 && d2) {
        ons++;
        heard[d1 & 127] = 1;
    } else if ((status & 0xF0) == 0x80 || (status & 0xF0) == 0x90)
        offs++;
}

static double now(void) { return (double)smp * 1000.0 / 44100.0; }
static void until(double at_ms)                  /* render up to a moment, to the block */
{
    static int32_t out[512];
    while (now() < at_ms) {
        h1_render(out, 256);
        smp += 256;
        if ((smp / 256) % 3 == 0)
            h1_frame(ms += 17);
    }
}
static void tap(int b) { h1_button(b, 1); until(now() + 30); h1_button(b, 0); until(now() + 30); }

static double bar_ms(void) { return 4.0 * 60000.0 / st.glo[0]; }   /* 4/4 */

/* n notes evenly through len ms from t0, each held a third of its slot; key0.. key0 + 11 */
static void take(double t0, double len, int n, int key0)
{
    int i;
    for (i = 0; i < n; i++) {
        double at = t0 + len * i / n;
        until(at);
        h1_key(key0 + i % 12, 1);
        until(at + len / n / 3.0);
        h1_key(key0 + i % 12, 0);
    }
}

static int count_pass(double len)                /* the note-ons one pass of the loop plays */
{
    double t = now();
    ons = offs = 0;
    memset(heard, 0, sizeof heard);
    counting = 1;
    until(t + len);
    counting = 0;
    return ons;
}

static int seq_gated(void)                       /* voices the loop holds down now */
{
    int v, n = 0;
    for (v = 0; v < NVOICE; v++)
        n += voices[v].stage && voices[v].gate && voices[v].seq;
    return n;
}

static void stop_and_clear(void)                 /* PLAY stops; REC held three seconds asks CLEAR ALL?, a tap says yes */
{
    tap(H1_BTN_PLAY);
    h1_button(H1_BTN_REC, 1); until(now() + 3300); h1_button(H1_BTN_REC, 0); until(now() + 100);
    tap(H1_BTN_REC);
}

/* jakehandy/h1#49: a long hold of REC, easily made by accident, cleared the whole tape. It asks now: the
 * tape is kept unless REC is tapped again, and not by any other button or by waiting. Called with a loop
 * recorded on T1 and stopped. */
static void clear_all_asks(void)
{
    int room = h1_loop_room();
    h1_button(H1_BTN_EDIT, 1); h1_encoder(H1_ENC_PRESET, 1); h1_button(H1_BTN_EDIT, 0);   /* to T2 */
    until(now() + 100);
    h1_button(H1_BTN_REC, 1); until(now() + 3300); h1_button(H1_BTN_REC, 0); until(now() + 100);
    CHECK(!strcmp(h1_toast_now(), "CLEAR ALL? TAP REC"), "REC held three seconds asks CLEAR ALL?");
    CHECK(h1_loop_room() == room, "asking, it has cleared only the track in hand (T2, empty)");
    until(now() + 3200);
    CHECK(h1_loop_room() == room, "left unanswered, the tape is kept");
    CHECK(!strcmp(h1_toast_now(), "TAPE KEPT"), "and it says so");
    h1_button(H1_BTN_REC, 1); until(now() + 3300); h1_button(H1_BTN_REC, 0); until(now() + 100);
    tap(H1_BTN_HOME);
    CHECK(h1_loop_room() == room, "another button answers no");
    /* (abektes/a22 review: the question faded after 1.25 s and its 3 s ran from the asking, REC still held, so a
     * tap meant to record could clear it all with nothing on screen; and only a button answered no) */
    h1_button(H1_BTN_REC, 1); until(now() + 4300); h1_button(H1_BTN_REC, 0); until(now() + 2000);
    CHECK(h1_clear_all_asked() && !strcmp(h1_toast_now(), "CLEAR ALL? TAP REC"),
          "REC held long and let go: two seconds on, the question is still on screen");
    until(now() + 1200);
    CHECK(!h1_clear_all_asked() && h1_loop_room() == room, "its three seconds run from letting go, then it is kept");
    h1_button(H1_BTN_REC, 1); until(now() + 3300); h1_button(H1_BTN_REC, 0); until(now() + 100);
    h1_encoder(H1_ENC_SELECT, 1); h1_encoder(H1_ENC_SELECT, -1);           /* (the tempo, up and back) */
    CHECK(!h1_clear_all_asked() && strcmp(h1_toast_now(), "CLEAR ALL? TAP REC"), "an encoder answers no");
    tap(H1_BTN_REC);
    CHECK(h1_loop_room() == room, "so the tap of REC after it does not clear the tape");
    tap(H1_BTN_REC);
    until(now() + 100);
    CHECK(h1_loop_room() == room, "(and REC tapped again leaves it as it was)");
    h1_button(H1_BTN_REC, 1); until(now() + 3300); h1_button(H1_BTN_REC, 0); until(now() + 100);
    h1_key(7, 1); until(now() + 100); h1_key(7, 0); until(now() + 100);
    CHECK(!h1_clear_all_asked() && h1_loop_room() == room, "a key answers no");
    h1_button(H1_BTN_REC, 1); until(now() + 3300); h1_button(H1_BTN_REC, 0); until(now() + 100);
    h1_toast("USB HOST");                                                   /* a toast of its own */
    CHECK(!h1_clear_all_asked(), "a toast that covers the question answers no");
    tap(H1_BTN_REC);
    CHECK(h1_loop_room() == room, "so the tap of REC after it does not clear the tape either");
    tap(H1_BTN_REC);
    until(now() + 100);
    h1_button(H1_BTN_REC, 1); until(now() + 3300); h1_button(H1_BTN_REC, 0); until(now() + 100);
    tap(H1_BTN_REC);
    CHECK(h1_loop_room() == NLEV / 2, "a tap of REC answers yes: the whole tape is empty");
    CHECK(!strcmp(h1_toast_now(), "TAPE CLEARED"), "and it says so");
    h1_button(H1_BTN_EDIT, 1); h1_encoder(H1_ENC_PRESET, -1); h1_button(H1_BTN_EDIT, 0);  /* back to T1 */
    until(now() + 100);
}

/* takes of n notes each onto a loop of 2^lb bars, another instrument for each, then one pass counted */
static double len_now;
static int layered(int lb, int takes, int n)
{
    double len, t0;
    int k;
    st.seq[1] = (uint8_t)lb;
    len = len_now = bar_ms() * (1 << lb);
    tap(H1_BTN_REC);                             /* armed: the first key starts the take */
    t0 = now() + 50;
    take(t0, len, n, 0);
    until(t0 + len + 40);                        /* round once: the first take plays */
    for (k = 1; k < takes; k++) {
        double start = t0 + len * k;
        h1_encoder(H1_ENC_PRESET, 1);            /* another instrument: another layer */
        until(start - 120);
        tap(H1_BTN_REC);                         /* onto the playing loop */
        take(start, len, n, 12 * (k & 1));
        until(start + len - 60);
        tap(H1_BTN_REC);                         /* the take ends as the loop comes round */
        until(start + len + 40);
    }
    until(t0 + len * takes + 20);
    return count_pass(len);
}

int main(void)
{
    int got, lb, i;
    uint32_t size, full0;
    const uint8_t *p;
    static uint8_t blob[H1_LOOP_MAX], old[12 + 8 * 384];

    h1_init(44100);
    h1_midi_out(midi);
    until(100);
    CHECK(h1_layers(), "LAYERS is on");
    CHECK(H1_LOOP_MAX == 12u + 8u * NLEV && NLEV == 2048, "the loop holds 2048 events");

    /* ---- the report: eight bars, light layers of eighths. Three fit in 384 events; the fourth did not ---- */
    got = layered(3, 4, 64);
    printf("looper: 8 bars, four layers of 64 notes: %d notes play back\n", got);
    CHECK(got == 4 * 64, "eight bars, four layers of 64 notes: every note plays back");
    CHECK(offs >= ons, "every note the loop starts it lets go");
    got = layered(3, 1, 0);                      /* (nothing more: just listen again) */
    tap(H1_BTN_PLAY);
    CHECK(h1_loop_room() < NLEV / 2, "the loop has notes in it");
    clear_all_asks();

    /* ---- longer loops: 16 and 32 bars (a22's longest), at a quick tempo to keep the test short ---- */
    st.glo[0] = 240;
    for (lb = 4; lb <= 5; lb++) {
        int n = 16 << (lb - 4);                  /* a note a bar */
        got = layered(lb, 2, n);
        printf("looper: %d bars, two layers of %d notes: %d notes play back in one pass\n", 1 << lb, n, got);
        CHECK(got == 2 * n, "a long loop plays every note of its takes, once a pass");
        stop_and_clear();
    }
    st.glo[0] = 112;

    /* ---- a full loop: says so, and keeps nothing half (a note-on without its note-off) ---- */
    st.glo[0] = 240;
    st.seq[3] = 0;                               /* SNAP FREE: no two notes on one tick */
    full0 = eng_loop_full();
    got = layered(5, 3, 400);                    /* 32 bars, 1200 notes: 1024 fit */
    printf("looper: 1200 notes into a loop of 1024: %d play back, room %d, toast \"%s\"\n", got, h1_loop_room(), h1_toast_now());
    CHECK(h1_loop_room() == 0, "the loop is full");
    CHECK(got == NLEV / 2, "every note that went in plays back");
    CHECK(eng_loop_full() != full0, "the notes turned away are counted");
    CHECK(offs >= ons, "every note the full loop starts it lets go");
    tap(H1_BTN_REC);                             /* one more onto the full loop: it says why it is not kept */
    h1_key(3, 1);
    until(now() + 80);
    CHECK(strcmp(h1_toast_now(), "TAPE FULL") == 0, "a note turned away by a full tape says TAPE FULL");
    h1_key(3, 0);
    until(now() + 80);
    tap(H1_BTN_REC);
    CHECK(h1_loop_room() == 0, "and the loop is as it was");

    /* ---- saved and loaded whole: 2048 events through h1_loop ---- */
    p = (const uint8_t *)h1_loop(&size);
    CHECK(size == H1_LOOP_MAX, "a full loop saves as H1_LOOP_MAX bytes");
    memcpy(blob, p, size);
    for (i = 0; i + 1 < NLEV; i++) {             /* in order of tick, as it lies in RAM */
        uint32_t a = blob[12 + 8 * i] | blob[13 + 8 * i] << 8, b = blob[12 + 8 * (i + 1)] | blob[13 + 8 * (i + 1)] << 8;
        if (a > b) break;
    }
    CHECK(i + 1 == NLEV, "the saved events are in order of tick");
    tap(H1_BTN_PLAY);
    stop_and_clear();
    CHECK(h1_loop_load(blob, size) == 0, "the 2048-event loop loads");
    CHECK(h1_loop_room() == 0, "and is whole");
    tap(H1_BTN_PLAY);
    got = count_pass(len_now);
    CHECK(got == NLEV / 2, "the loaded loop plays every note");
    tap(H1_BTN_PLAY);
    until(now() + 300);
    CHECK(seq_gated() == 0, "stopped, the loop holds no note down");
    st.glo[0] = 112;
    st.seq[3] = 1;

    /* ---- recorded on 4 bars, the tape shortened to 1, saved and loaded, lengthened again: every note is
     * there (the loader used to drop events past the saved length, which the session still had) ---- */
    stop_and_clear();
    got = layered(2, 1, 16);                     /* 4 bars, 16 notes */
    CHECK(got == 16, "4 bars of 16 notes play back");
    tap(H1_BTN_PLAY);
    until(now() + 200);
    st.seq[1] = 0;                               /* BARS 1: three quarters of the notes lie past the end */
    tap(H1_BTN_PLAY);                            /* (played a moment: the tape takes on its new length) */
    until(now() + 300);
    tap(H1_BTN_PLAY);
    until(now() + 200);
    p = (const uint8_t *)h1_loop(&size);
    memcpy(blob, p, size);
    CHECK((int)(blob[8] | blob[9] << 8) == seq_bar_ticks(), "the tape saves its new length: one bar");
    stop_and_clear();
    CHECK(h1_loop_load(blob, size) == 0, "the shortened tape loads");
    CHECK(h1_loop_room() == NLEV / 2 - 16, "with every one of its 16 notes, past the end or not");
    st.seq[1] = 2;                               /* BARS 4 again */
    tap(H1_BTN_PLAY);
    got = count_pass(bar_ms() * 4);
    printf("looper: recorded on 4 bars, saved on 1, played on 4: %d of 16 notes\n", got);
    CHECK(got == 16, "lengthened again after a load, every note plays");
    tap(H1_BTN_PLAY);

    /* ---- the length's bounds: 32 bars of 5/4 (15360 ticks) is the longest a loop loads; BARS is kept in range ---- */
    memset(old, 0, sizeof old);
    old[0] = 'A'; old[1] = '2'; old[2] = '2'; old[3] = 'T'; old[4] = 1;
    old[8] = (uint8_t)15360; old[9] = (uint8_t)(15360 >> 8);
    CHECK(h1_loop_load(old, 12) == 0, "32 bars of 5/4 load");
    old[8] = (uint8_t)15361; old[9] = (uint8_t)(15361 >> 8);
    CHECK(h1_loop_load(old, 12) != 0, "and nothing longer");
    old[0] = 'H'; old[1] = '1'; old[2] = 'L'; old[3] = 'P';
    old[8] = 96; old[9] = 0;
    CHECK(h1_loop_load(old, 12) != 0, "an h1 loop is not a22's");
    {
        static uint8_t sb[sizeof(H1State)];
        const void *sp;
        uint32_t ssz;
        st.seq[1] = 5;
        sp = h1_state(&ssz);
        memcpy(sb, sp, ssz);
        st.seq[1] = 1;
        CHECK(h1_state_load(sb, ssz) == 0 && st.seq[1] == 5, "BARS 32 is saved with the state");
        sb[offsetof(H1State, seq) + 1] = 9;
        CHECK(h1_state_load(sb, ssz) == 0 && st.seq[1] <= 5, "and a BARS out of range comes back in range");
    }

    printf("looper: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
