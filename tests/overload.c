/* a22's guard against running late: the port tells the core, once a render, whether the half before went out late
 * (h1_late). One late half, or late halves with one in time between them, shed nothing; two in a row shed one voice,
 * which dies away in about 10 ms without a click, and no other goes while it does. The voice is the quietest of those
 * already let go, else the oldest the player is not holding (the tape's, another track's), else the oldest. Running
 * late half after half is what made HAZE with the tape playing starve the FM-1 (a tester's report).
 * Run by tests/run_tests.sh. */
#include <stdio.h>
#include <string.h>
#include "../core/internal.h"
#include "../core/h1.h"

static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

static uint32_t ms;
static int32_t out[512];
static int jump;                                  /* the largest step from one sample to the next, while it runs */
static void half(int late)                        /* one half of 256 frames, as the FM-1 renders it */
{
    static int32_t last;
    static int n;
    int i;
    h1_late(late);
    h1_render(out, 256);
    for (i = 0; i < 256; i++) {
        int d = out[2 * i] - last;
        if (d < 0) d = -d;
        if (d > jump) jump = d;
        last = out[2 * i];
    }
    if (n++ % 3 == 0) h1_frame(ms += 17);
}
static void go(int halves) { while (halves--) half(0); }

static int shedding(void)
{
    int i, n = 0;
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage && voices[i].shed) n++;
    return n;
}

static void fresh(void)
{
    int k;
    for (k = 0; k < H1_NKEYS; k++) h1_key(k, 0);
    h1_init(44100);
    h1_two_cores(0, 0);
    go(10);
}

static void track(int d)                          /* EDIT held, PRESETS turned, EDIT let go */
{
    h1_button(H1_BTN_EDIT, 1);
    h1_encoder(H1_ENC_PRESET, d);
    h1_button(H1_BTN_EDIT, 0);
}

int main(void)
{
    int k, i, quiet0 = 0, shed_quiet = 1, let = -1;
    uint32_t oldest = 0xFFFFFFFFu;

    /* eight keys held: every voice busy, all under the player's hands */
    fresh();
    for (k = 0; k < 8; k++) {
        h1_key(k * 3, 1);
        go(2);
    }
    go(40);
    CHECK(h1_voices_active() == 8u, "eight keys hold eight voices");
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stamp < oldest) oldest = voices[i].stamp;

    half(1);                                      /* one late half: a passing peak */
    go(1);
    CHECK(h1_voices_shed() == 0u && shedding() == 0, "one late half sheds nothing");
    half(1); half(0); half(1); half(0); half(1);  /* late, but never twice in a row */
    CHECK(h1_voices_shed() == 0u, "late halves with one in time between shed nothing");

    half(1);                                      /* (the one before was late too) two in a row: one voice, the */
    CHECK(h1_voices_shed() == 1u && shedding() == 1, "two late halves in a row shed one voice");   /* oldest */
    for (i = 0; i < NVOICE; i++)
        if (voices[i].shed) CHECK(voices[i].stamp == oldest, "with every voice held, the oldest is shed");
    half(1);                                      /* still late while it dies away: no second voice */
    CHECK(h1_voices_shed() == 1u, "no second voice while the first dies away");
    jump = 0;
    go(4);                                        /* 23 ms: 32 blocks, the fade takes 14 */
    CHECK(shedding() == 0 && h1_voices_active() == 7u, "the shed voice is gone within 23 ms, the rest play on");
    CHECK(jump < 600000, "letting it go does not click");
    half(1); half(1);                             /* late again, twice: the next one goes */
    CHECK(h1_voices_shed() == 2u, "late again: a second voice");
    go(4);
    CHECK(h1_voices_active() == 6u, "six voices left");

    /* a key let go first: that one goes before any held one */
    h1_key(21, 0);
    go(2);
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage == 4 && !voices[i].shed) let = i;
    CHECK(let >= 0, "the key let go is releasing");
    half(1); half(1);
    CHECK(h1_voices_shed() == 3u && let >= 0 && voices[let].shed, "the voice already let go is shed, not a held one");
    go(4);

    /* of two let go, the quieter goes */
    fresh();
    h1_key(0, 1); go(2); h1_key(0, 0);            /* let go first: quieter by the time the other is let go */
    go(20);
    h1_key(7, 1); go(2); h1_key(7, 0);
    go(1);
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage == 4 && voices[i].note == voices[0].note) quiet0 = i;
    {
        int q = -1;
        for (i = 0; i < NVOICE; i++)
            if (voices[i].stage == 4 && (q < 0 || voices[i].env < voices[q].env)) q = i;
        half(1); half(1);
        shed_quiet = q >= 0 && voices[q].shed;
    }
    (void)quiet0;
    CHECK(shed_quiet, "of two voices let go, the quieter is shed");

    /* the tape's notes go before the keys the player holds, even when the held ones are older */
    fresh();
    h1_control_set(H1_ENC_PRESET, 0);
    go(4);
    h1_button(H1_BTN_REC, 1); h1_button(H1_BTN_REC, 0);
    go(4);
    for (k = 0; k < 4; k++) {                     /* a held chord recorded on T1 */
        h1_key(k * 4, 1);
        go(1);
    }
    go(60);
    for (k = 0; k < 4; k++) h1_key(k * 4, 0);
    go(200);
    h1_button(H1_BTN_REC, 1); h1_button(H1_BTN_REC, 0);   /* (closes the take) */
    go(4);
    if (!(h1_button_leds() >> H1_BTN_PLAY & 1u)) {           /* and the tape plays it */
        h1_button(H1_BTN_PLAY, 1); h1_button(H1_BTN_PLAY, 0);
        go(4);
    }
    CHECK(h1_button_leds() >> H1_BTN_PLAY & 1u, "the tape plays");
    track(1);                                     /* T2 in hand: four keys held there */
    go(4);
    for (k = 0; k < 4; k++) {
        h1_key(1 + k * 4, 1);
        go(1);
    }
    for (i = 0; i < 2000 && !(h1_voices_active() > 4u); i++)   /* until the tape's chord sounds beside them */
        go(1);
    {
        int tape_on = 0, held_on = 0, shed_tape = 0, shed_held = 0;
        for (i = 0; i < NVOICE; i++) {
            if (!voices[i].stage) continue;
            if (voices[i].seq || voices[i].trk != 1) tape_on++;
            else if (voices[i].gate) held_on++;
        }
        CHECK(tape_on > 0 && held_on == 4, "T1's tape plays beside four keys held on T2");
        half(1); half(1);
        for (i = 0; i < NVOICE; i++)
            if (voices[i].shed) {
                if (voices[i].seq || voices[i].trk != 1) shed_tape++;
                else shed_held++;
            }
        CHECK(shed_tape == 1 && shed_held == 0, "a tape note is shed, not a key the player holds");
    }

    /* every key let go: silence; a new press plays and is not shed */
    for (k = 0; k < H1_NKEYS; k++) h1_key(k, 0);
    h1_button(H1_BTN_PLAY, 1); h1_button(H1_BTN_PLAY, 0);
    go(800);
    CHECK(h1_voices_active() == 0u, "every key let go and the tape stopped: silence");
    h1_key(5, 1);
    go(4);
    CHECK(h1_voices_active() == 1u, "a key pressed after plays");
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage) CHECK(!voices[i].shed, "a new note is not shed");

    printf("overload: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
