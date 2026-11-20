/* Chord memory: one key plays a chord. Run by tests/run_tests.sh: the core alone, its panel worked as a
 * hand would, what is played counted on MIDI out and in the voices.
 *
 *   modes       OFF one note; SHAPE fixed chords; SCALE chords from the KEYS root and scale (CHROM: major);
 *               MEMORY a captured shape, transposed
 *   voicing     CLOSE, OPEN, DROP2, +BASS
 *   release     a key lets go of the chord it started, whatever the setting is by then; two chords that
 *               share notes hold them until both are let go
 *   strum       the notes come one after another, and a key let go early leaves nothing behind
 *   capture     EDIT + GLO with keys held stores them (MEMORY); with none held it turns chords off and on
 *   the rest    MIDI in is not made into chords; MONO (mono) plays one note; the tape records the chord;
 *               HOLD latches it; HUSH clears it; the CHORD page; saved and loaded, nonsense clamped */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../core/internal.h"
#include "../core/h1.h"

static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

static uint32_t t_ms;
static int on_n[128], off_n[128], ons, offs, order[64], norder;
static void mo(uint8_t s, uint8_t a, uint8_t b)
{
    if ((s & 0xF0) == 0x90 && b) {
        on_n[a & 127]++;
        ons++;
        if (norder < 64) order[norder++] = a;
    } else if ((s & 0xF0) == 0x80 || (s & 0xF0) == 0x90) {
        off_n[a & 127]++;
        offs++;
    }
}
static void clear(void) { memset(on_n, 0, sizeof on_n); memset(off_n, 0, sizeof off_n); ons = offs = norder = 0; }
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
static void go_fine(int msec)                    /* in 32-sample steps, for the strum's timing */
{
    static int32_t out[64];
    int blocks = msec * 441 / 320 + 1, i;
    for (i = 0; i < blocks; i++)
        h1_render(out, 32);
}
static void tap(int b) { h1_button(b, 1); go(40); h1_button(b, 0); go(40); }
static void fresh(void)
{
    int k;
    for (k = 0; k < H1_NKEYS; k++)
        h1_key(k, 0);
    h1_init(44100);
    h1_two_cores(0, 0);
    h1_midi_out(mo);
    go(100);
    clear();
}
static int gated(void)
{
    int v, n = 0;
    for (v = 0; v < NVOICE; v++)
        n += voices[v].stage && voices[v].gate;
    return n;
}
static int only(const int *notes, int n)         /* exactly these note-ons, once each, and no others */
{
    int i, got = 0;
    for (i = 0; i < n; i++) {
        if (on_n[notes[i]] != 1) return 0;
        got++;
    }
    return ons == got;
}
static int offs_are(const int *notes, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (off_n[notes[i]] != 1) return 0;
    return offs == n;
}
static void chord(int mode, int type, int voicing, int strum)
{
    st.chord[0] = (uint8_t)mode;
    st.chord[1] = (uint8_t)type;
    st.chord[2] = (uint8_t)voicing;
    st.chord[3] = (uint8_t)strum;
}
static void key(int k, int ms_held)
{
    h1_key(k, 1);
    go(ms_held);
    h1_key(k, 0);
    go(200);
}

int main(void)
{
    /* ---- the modes ---- */
    {
        static const int C[1] = {60}, MAJ[3] = {60, 64, 67}, MIN7[4] = {60, 63, 67, 70};
        fresh();
        CHECK(st.chord[0] == CHORD_OFF, "chords are off on a new a22");
        key(7, 100);                             /* (key 7 is note 60) */
        CHECK(only(C, 1) && offs_are(C, 1), "OFF: a key is one note");
        fresh();
        chord(CHORD_SHAPE, 0, 0, 0);
        h1_key(7, 1);
        go(100);
        CHECK(only(MAJ, 3), "SHAPE MAJ: C, E, G");
        CHECK(gated() == 3, "three voices sound");
        h1_key(7, 0);
        go(200);
        CHECK(offs_are(MAJ, 3) && gated() == 0, "and all three are let go");
        fresh();
        chord(CHORD_SHAPE, 4, 0, 0);
        key(7, 100);
        CHECK(only(MIN7, 4), "SHAPE MIN7: C, Eb, G, Bb");
    }
    {
        static const int AMIN[3] = {57, 60, 64}, DMIN[3] = {62, 65, 69}, CMAJ[3] = {60, 64, 67},
                         CMAJ7[4] = {60, 64, 67, 71};
        fresh();
        st.keys[0] = 9;                          /* A */
        st.keys[1] = 2;                          /* MINOR */
        chord(CHORD_SCALE, 0, 0, 0);
        key(4, 100);                             /* A3 */
        CHECK(only(AMIN, 3), "SCALE TRIAD in A minor, on A: A, C, E");
        clear();
        key(9, 100);                             /* D4 */
        CHECK(only(DMIN, 3), "on D: D, F, A");
        fresh();
        chord(CHORD_SCALE, 0, 0, 0);             /* CHROM: the major scale on the root */
        key(7, 100);
        CHECK(only(CMAJ, 3), "SCALE TRIAD with CHROM: C major");
        clear();
        chord(CHORD_SCALE, 1, 0, 0);
        key(7, 100);
        CHECK(only(CMAJ7, 4), "SCALE 7TH: C, E, G, B");
    }

    /* ---- voicing ---- */
    {
        static const int OPEN[3] = {60, 67, 76}, DROP2[3] = {52, 60, 67}, BASS[4] = {48, 60, 64, 67};
        fresh();
        chord(CHORD_SHAPE, 0, 1, 0);
        key(7, 100);
        CHECK(only(OPEN, 3), "OPEN: the middle note an octave up");
        clear();
        chord(CHORD_SHAPE, 0, 2, 0);
        key(7, 100);
        CHECK(only(DROP2, 3), "DROP2: the second from the top an octave down");
        clear();
        chord(CHORD_SHAPE, 0, 3, 0);
        key(7, 100);
        CHECK(only(BASS, 4), "+BASS: the root an octave below too");
    }

    /* ---- release ---- */
    {
        static const int MAJ[3] = {60, 64, 67}, C[1] = {60}, EGB[3] = {64, 67, 71};
        fresh();
        chord(CHORD_SHAPE, 0, 0, 0);
        h1_key(7, 1);
        go(100);
        chord(CHORD_OFF, 0, 0, 0);               /* the setting changes while the key is down */
        h1_key(7, 0);
        go(200);
        CHECK(offs_are(MAJ, 3) && gated() == 0, "a key lets go of the chord it started, though chords are off by then");

        fresh();
        chord(CHORD_SHAPE, 0, 0, 0);
        h1_key(7, 1);                            /* C major: 60 64 67 */
        go(50);
        chord(CHORD_SHAPE, 1, 0, 0);
        h1_key(11, 1);                           /* E minor: 64 67 71, two of them shared */
        go(50);
        CHECK(ons == 4 && on_n[71] == 1, "a second chord sounds only the note the first does not");
        h1_key(7, 0);
        go(100);
        CHECK(offs_are(C, 1), "letting go of the first ends only what the second does not hold");
        CHECK(gated() == 3, "the second chord sounds on, whole");
        clear();
        h1_key(11, 0);
        go(200);
        CHECK(offs_are(EGB, 3) && gated() == 0, "and letting go of the second ends the rest");
    }

    /* ---- strum ---- */
    {
        fresh();
        chord(CHORD_SHAPE, 0, 0, 50);            /* 30 ms between notes */
        h1_key(7, 1);
        go_fine(5);
        CHECK(ons == 1 && on_n[60] == 1, "strummed: the lowest note at once");
        go_fine(35);
        CHECK(ons == 2 && on_n[64] == 1, "the next 30 ms later");
        go_fine(40);
        CHECK(ons == 3 && order[0] == 60 && order[1] == 64 && order[2] == 67, "then the last, upward");
        h1_key(7, 0);
        go(200);
        CHECK(offs == 3 && gated() == 0, "all let go");

        fresh();
        chord(CHORD_SHAPE, 0, 0, 100);           /* 60 ms between notes */
        h1_key(7, 1);
        go_fine(10);
        h1_key(7, 0);                            /* let go before the strum is through */
        go(400);
        CHECK(ons == 1 && offs == 1 && gated() == 0, "a key let go mid-strum: what had not sounded never does");
    }

    /* ---- capture ---- */
    {
        static const int G[3] = {55, 59, 62};
        fresh();
        h1_key(0, 1);                            /* F3 A3 C4: 53 57 60 */
        h1_key(4, 1);
        h1_key(7, 1);
        go(60);
        h1_button(H1_BTN_EDIT, 1);
        go(30);
        h1_button(H1_BTN_GLO, 1);
        go(30);
        h1_button(H1_BTN_GLO, 0);
        h1_button(H1_BTN_EDIT, 0);
        go(60);
        h1_key(0, 0);
        h1_key(4, 0);
        h1_key(7, 0);
        go(200);
        CHECK(st.chord[0] == CHORD_MEMORY, "EDIT + GLO with keys held: MEMORY");
        CHECK(st.chord_n == 3 && st.chord_mem[0] == 0 && st.chord_mem[1] == 4 && st.chord_mem[2] == 7,
              "the shape is kept as 0, 4, 7 from its lowest note");
        CHECK(h1_page() == PG_HOME, "and no page opens");
        clear();
        key(2, 100);                             /* G3: 55 */
        CHECK(only(G, 3), "a key plays the shape from it: G, B, D");
        h1_button(H1_BTN_EDIT, 1);
        go(30);
        h1_button(H1_BTN_GLO, 1);
        go(30);
        h1_button(H1_BTN_GLO, 0);
        h1_button(H1_BTN_EDIT, 0);
        go(60);
        CHECK(st.chord[0] == CHORD_OFF, "EDIT + GLO with no key held: chords off");
        h1_button(H1_BTN_EDIT, 1);
        go(30);
        h1_button(H1_BTN_GLO, 1);
        go(30);
        h1_button(H1_BTN_GLO, 0);
        h1_button(H1_BTN_EDIT, 0);
        go(60);
        CHECK(st.chord[0] == CHORD_MEMORY && st.chord_n == 3, "and again: back on, the shape kept");
    }

    /* ---- MIDI in, MONO, the tape, HOLD, HUSH ---- */
    {
        SeqInfo si;
        int pass;
        fresh();
        chord(CHORD_SHAPE, 0, 0, 0);
        h1_midi(0x90, 60, 100);
        go(100);
        CHECK(gated() == 1, "a note over MIDI is not made into a chord");
        h1_midi(0x80, 60, 0);
        go(200);

        fresh();
        h1_control_set(H1_ENC_PRESET, 7);        /* MONO */
        chord(CHORD_SHAPE, 0, 0, 0);
        h1_key(7, 1);
        go(100);
        CHECK(gated() == 1 && ons == 1 && on_n[60] == 1, "MONO is mono: the chord's root alone");
        h1_key(7, 0);
        go(200);

        fresh();
        chord(CHORD_SHAPE, 0, 0, 0);
        st.seq[1] = 0;                           /* a 1-bar tape */
        tap(H1_BTN_REC);
        key(7, 150);
        tap(H1_BTN_REC);
        eng_seq_info(&si);
        CHECK(si.loop_state == LOOP_PLAY, "a chord recorded, the tape plays");
        chord(CHORD_OFF, 0, 0, 0);               /* what was recorded does not need chords on */
        clear();
        go(4 * 60000 / st.glo[0] + 100);
        pass = ons;
        CHECK(pass == 3 && on_n[60] == 1 && on_n[64] == 1 && on_n[67] == 1, "the tape plays the chord back");
        tap(H1_BTN_PLAY);
        go(300);
        CHECK(gated() == 0, "stopped, nothing held");

        fresh();
        chord(CHORD_SHAPE, 0, 0, 0);
        st.keys[3] = 1;                          /* HOLD */
        key(7, 80);
        CHECK(gated() == 3, "HOLD: the chord stays after the key is let go");
        clear();
        h1_key(9, 1);                            /* the next key lets the last go */
        go(100);
        CHECK(off_n[60] == 1 && off_n[64] == 1 && off_n[67] == 1 && on_n[62] == 1, "the next key lets it go and plays its own");
        h1_key(9, 0);
        st.keys[3] = 0;
        eng_post(EV_UNLATCH, 0);
        go(200);

        fresh();
        chord(CHORD_SHAPE, 0, 0, 60);
        h1_key(7, 1);
        go_fine(10);
        h1_button(H1_BTN_HOME, 1);               /* HUSH, mid-strum */
        go(1000);
        h1_button(H1_BTN_HOME, 0);
        go(300);
        CHECK(gated() == 0, "HUSH: nothing sounds");
        clear();
        h1_key(7, 0);
        go(300);
        CHECK(ons == 0 && gated() == 0, "and the key let go afterwards starts nothing");
    }

    /* ---- the CHORD page ---- */
    {
        const char *name, *text;
        int lo, hi;
        fresh();
        tap(H1_BTN_GLO);
        h1_encoder(H1_ENC_SELECT, 1);
        h1_encoder(H1_ENC_SELECT, 1);
        go(60);
        h1_control(H1_ENC_K1, &lo, &hi, &name, &text);
        CHECK(h1_page() == PG_GLO && strcmp(name, "MODE") == 0, "GLO's third page: CHORD, MODE first");
        h1_encoder(H1_ENC_K1, 3);
        go(60);
        CHECK(st.chord[0] == CHORD_SHAPE, "K1 turns the mode to SHAPE");
        h1_encoder(H1_ENC_K2, 7);
        go(60);
        CHECK(st.chord[1] == 7, "K2 the shape");
        h1_encoder(H1_ENC_K1, -1);
        go(60);
        CHECK(st.chord[0] == CHORD_SCALE && st.chord[1] < 6, "back to SCALE: the type is kept in SCALE's range");
        h1_control(H1_ENC_K4, &lo, &hi, &name, &text);
        CHECK(strcmp(name, "STRUM") == 0 && hi == 100, "K4 is STRUM");
    }

    /* ---- saved and loaded ---- */
    {
        static uint8_t buf[sizeof(H1State)];
        uint32_t size;
        fresh();
        chord(CHORD_MEMORY, 2, 3, 40);
        st.chord_n = 4;
        st.chord_mem[1] = 3; st.chord_mem[2] = 7; st.chord_mem[3] = 10;
        { const void *src = h1_state(&size); memcpy(buf, src, size); }
        fresh();
        CHECK(h1_state_load(buf, size) == 0, "the state loads");
        CHECK(st.chord[0] == CHORD_MEMORY && st.chord[2] == 3 && st.chord[3] == 40 && st.chord_n == 4 &&
              st.chord_mem[3] == 10, "the chord settings and the shape come back");
        buf[offsetof(H1State, chord) + 0] = 200;
        buf[offsetof(H1State, chord) + 2] = 200;
        buf[offsetof(H1State, chord) + 3] = 200;
        buf[offsetof(H1State, chord_n)] = 99;
        CHECK(h1_state_load(buf, size) == 0, "nonsense in the chord settings still loads");
        CHECK(st.chord[0] <= CHORD_SHAPE && st.chord[2] <= 3 && st.chord[3] <= 100 && st.chord_n >= 1 &&
              st.chord_n <= CHORD_MAX && st.chord_mem[0] == 0, "and comes back in range");
        clear();
        key(7, 100);
        CHECK(ons >= 1 && ons <= CHORD_MAX && gated() == 0, "and plays");
    }

    printf("chords: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
