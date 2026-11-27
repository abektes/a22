/* Chord pads: eight chords under eight keys, from 24 sets. Run by tests/run_tests.sh: the core alone, its panel
 * worked as a hand would, what is played counted on MIDI out and in the voices.
 *
 *   sets        every pad of every set in all twelve roots: in range, ascending, named, numbered */
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

static void sets(void)
{
    int s, p, r, i, n, nt[5];
    char num[9];
    for (s = 0; s < NCHSET; s++) {
        CHECK(CH_SET_NAMES[s][0] && strlen(CH_SET_NAMES[s]) <= 8, "a set has a name of 1 to 8 characters");
        for (p = 0; p < 8; p++) {
            CHECK(CH_SETS[s].pad[p].off < 12 && CH_SETS[s].pad[p].type < NCHTYPE, "a pad is in range");
            for (r = 0; r < 12; r++) {
                n = ch_pad_notes(s, p, r, 0, nt);
                CHECK(n >= 3 && n <= 5 && n <= CHORD_MAX, "three to five notes");
                CHECK(nt[0] == 48 + (r + CH_SETS[s].pad[p].off) % 12, "the root, in the octave from C3");
                CHECK(ch_pad_pc(s, p, r) == nt[0] % 12, "its pitch class");
                for (i = 1; i < n; i++) CHECK(nt[i] > nt[i - 1] && nt[i] <= 127, "ascending");
            }
            i = ch_pad_numeral(s, p, num);
            CHECK(num[0] && (i == CH_NUM_BORROWED) == !strcmp(num, "BORROWED"), "every pad has a numeral");
        }
    }
    n = ch_pad_notes(10, 0, 0, 0, nt);   /* LOFI 2, pad 1: C MIN9 */
    CHECK(!strcmp(CH_SET_NAMES[10], "LOFI 2") && n == 5 && nt[0] == 48 && nt[1] == 51 && nt[2] == 55 &&
          nt[3] == 58 && nt[4] == 62, "LOFI 2 pad 1 is C minor nine");
    CHECK(ch_pad_numeral(10, 0, num) == CH_NUM_MINOR && !strcmp(num, "I"), "I, underlined");
    CHECK(ch_pad_numeral(10, 2, num) == CH_NUM_MAJOR && !strcmp(num, "VI"), "VI in a minor set");
    CHECK(ch_pad_numeral(10, 7, num) == CH_NUM_BORROWED, "the C# chord is borrowed");
    CHECK(ch_pad_notes(0, 0, 0, 1, nt) == 3 && nt[0] == 60, "an octave up");
    CHECK(ch_pad_notes(-1, 8, 0, 0, nt) == 3 && nt[0] == 48, "out of range: set 0, pad 0");
}

int main(void)
{
    sets();
    printf("pads: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
