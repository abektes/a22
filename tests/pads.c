/* Chord pads: eight chords under eight keys, from 24 sets. Run by tests/run_tests.sh: the core alone, its panel
 * worked as a hand would, what is played counted on MIDI out and in the voices.
 *
 *   sets        every pad of every set in all twelve roots: in range, ascending, named, numbered
 *   play        MODE PADS: the eight white keys from the lowest F play the set's chords; every note-off matches,
 *               whatever changes while a key is held; MIDI in, SAVE, FX, MONO and drums
 *   latch       HOLD latches pads; HUSH and a state load leave nothing sounding
 *   sequencers  the tape, STEP and RIFF take a pad's notes
 *   smooth      SMOOTH voicing: each chord close to the last, and OCT still moves it
 *   saved       MODE PADS, the set and SMOOTH in the saved state, nonsense clamped
 *   controls    the CHORDS page: hold ARP, its two subpages' columns, EDIT + GLO, GLO without CHORD */
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

static void pads_on(int set) { chord(CHORD_PADS, 0, 0, 0); st.rsv[1] = (uint8_t)set; }
static int matched(void)
{
    int n;
    for (n = 0; n < 128; n++) if (on_n[n] != off_n[n]) return 0;
    return gated() == 0;
}
static void down(int k) { h1_key(k, 1); go(60); }
static void up(int k)   { h1_key(k, 0); go(200); }

static void play(void)
{
    static const int C[3] = {48, 52, 55}, D[3] = {50, 54, 57}, ONE[1] = {54}, MID[1] = {48};
    int i;
    fresh(); pads_on(0);
    down(0); CHECK(only(C, 3) && gated() == 3, "pad 1 of POP 1 is C major");
    up(0);   CHECK(offs_are(C, 3) && gated() == 0, "and ends");
    fresh(); pads_on(0); key(1, 60);
    CHECK(only(ONE, 1) && matched(), "a key beside the pads is one note");
    fresh(); pads_on(0); st.keys[0] = 2; key(0, 60);
    CHECK(only(D, 3), "ROOT moves the pads");
    fresh(); pads_on(0); st.keys[1] = 2; st.keys[0] = 0; key(6, 60);
    CHECK(on_n[53] == 1 && on_n[57] == 1 && on_n[60] == 1, "pads are never snapped to the scale");

    fresh(); pads_on(0); down(0);                 /* everything changes while it is held */
    st.rsv[1] = 9; st.keys[0] = 5; tap(H1_BTN_OCTUP); chord(CHORD_OFF, 0, 0, 0);
    up(0); CHECK(offs_are(C, 3) && matched(), "the release ends what the press started");

    fresh(); pads_on(0); down(6); down(11); up(6);        /* two pads on F: F MAJ and F ADD9 */
    CHECK(gated() == 4 && on_n[53] == 1, "shared notes sound once");
    up(11); CHECK(matched(), "and end with the last");
    fresh(); pads_on(0); down(11); down(14); up(11);      /* a played G4, which F ADD9 also holds */
    CHECK(gated() == 1 && off_n[67] == 0, "the played key keeps its note");
    up(14); CHECK(matched() && on_n[67] == 1, "one on, one off");
    fresh(); pads_on(0); down(14); down(11); up(14);
    CHECK(gated() == 4 && off_n[67] == 0, "either order");
    up(11); CHECK(matched(), "all ended");

    fresh(); pads_on(0); down(0);                 /* the track changes under a held pad */
    h1_button(H1_BTN_EDIT, 1); go(30); h1_encoder(H1_ENC_PRESET, 1); go(60); h1_button(H1_BTN_EDIT, 0); go(60);
    up(0); CHECK(st.track == 1 && matched(), "held pad, new track: ends");

    fresh(); pads_on(0); h1_key(0, 1); h1_key(2, 1); go(60);      /* EDIT + GLO with two pads held */
    h1_button(H1_BTN_EDIT, 1); go(30); tap(H1_BTN_GLO); h1_button(H1_BTN_EDIT, 0); go(60);
    h1_key(0, 0); h1_key(2, 0); go(200);
    CHECK(st.chord[0] == CHORD_OFF && st.chord_n == 3 && st.chord_mem[1] == 4 && matched(),
          "pad keys are no notes held: nothing captured, chords off");

    fresh(); pads_on(0); h1_midi(0x90, 48, 100); go(100);
    CHECK(gated() == 1, "MIDI in is one note");
    h1_midi(0x80, 48, 0); go(200); CHECK(matched(), "and ends");

    fresh(); pads_on(0); h1_button(H1_BTN_SAVE, 1); go(40); key(0, 60); h1_button(H1_BTN_SAVE, 0); go(60);
    CHECK(st.snap[0].used && ons == 0, "SAVE + key wins");
    fresh(); pads_on(0); h1_button(H1_BTN_FX, 1); go(40); key(0, 60); h1_button(H1_BTN_FX, 0); go(60);
    CHECK(ons == 0, "FX + key wins");
    fresh(); pads_on(0); down(0); h1_button(H1_BTN_SAVE, 1); go(40); up(0); h1_button(H1_BTN_SAVE, 0); go(60);
    CHECK(matched() && !st.snap[0].used, "SAVE down after the press: the release is still the pad's");
    fresh(); pads_on(0); down(0); h1_button(H1_BTN_FX, 1); go(40); up(0); h1_button(H1_BTN_FX, 0); go(60);
    CHECK(matched(), "FX too");

    fresh(); pads_on(0); h1_control_set(H1_ENC_PRESET, 7); go(60); clear(); key(0, 60);
    CHECK(only(MID, 1) && matched(), "MONO plays the pad's root");
    fresh(); pads_on(0); h1_control_set(H1_ENC_PRESET, INST_KIT); go(60); clear();
    down(0);
    CHECK(ons == 1 && on_n[53] == 1 && !strcmp(h1_toast_now(), "NOT ON DRUMS"), "KIT: the key plays a drum");
    up(0); go(2000); down(0);
    CHECK(!h1_toast_now()[0], "said once, not on every hit");
    up(0); h1_control_set(H1_ENC_PRESET, 0); go(60); h1_control_set(H1_ENC_PRESET, INST_KIT); go(2000); down(0);
    CHECK(!strcmp(h1_toast_now(), "NOT ON DRUMS"), "and again after arriving on drums");
    up(0);
    for (i = 0; i < 2; i++) {                    /* the engine changes under a held pad */
        fresh(); pads_on(0); down(0); h1_control_set(H1_ENC_PRESET, i ? 7 : INST_KIT); go(60); up(0);
        CHECK(off_n[48] == 1 && off_n[52] == 1 && off_n[55] == 1 && gated() == 0, "held pad, new engine: ends");
    }

    fresh(); pads_on(0); down(0);
    for (i = 0; i < 80; i++) eng_post(EV_PUNCH, 0);      /* the queue full, nothing rendered */
    up(0); CHECK(matched(), "a pad's off gets through a full queue");
}

static void latch(void)
{
    static uint8_t buf[sizeof(H1State)];
    uint32_t size;
    fresh(); pads_on(0); st.keys[3] = 1;
    key(0, 60); CHECK(gated() == 3, "HOLD latches a pad");
    clear(); key(6, 60); CHECK(off_n[48] == 1 && off_n[55] == 1 && on_n[53] == 1, "the next pad replaces it");
    st.keys[3] = 0; eng_post(EV_UNLATCH, 0); go(300); CHECK(gated() == 0, "unlatched");
    fresh(); pads_on(0); st.keys[3] = 1; down(11); down(14); up(11); up(14);
    st.keys[3] = 0; eng_post(EV_UNLATCH, 0); go(300); CHECK(matched(), "a latched pad and key on one pitch");
    fresh(); pads_on(0); st.keys[3] = 1; key(0, 60); down(6);
    h1_button(H1_BTN_HOME, 1); go(1000); h1_button(H1_BTN_HOME, 0); go(200); clear(); up(6);
    CHECK(gated() == 0 && ons == 0, "HUSH with pads held and latched");
    fresh(); pads_on(0); { const void *s = h1_state(&size); memcpy(buf, s, size); }
    down(0); CHECK(h1_state_load(buf, size) == 0, "a load under a held pad"); go(100); up(0);
    CHECK(gated() == 0, "nothing left sounding");
}

static void sequencers(void)
{
    SeqInfo si;
    fresh(); pads_on(0); st.seq[1] = 0;
    tap(H1_BTN_REC); key(0, 150); tap(H1_BTN_REC); chord(CHORD_OFF, 0, 0, 0); clear();
    go(4 * 60000 / st.glo[0] + 100);
    CHECK(ons == 3 && on_n[48] == 1 && on_n[52] == 1 && on_n[55] == 1, "the tape plays the pad's chord");
    tap(H1_BTN_PLAY); go(300);
    fresh(); pads_on(0);
    h1_button(H1_BTN_SEQ, 1); go(700); h1_button(H1_BTN_SEQ, 0); go(60); tap(H1_BTN_REC);
    eng_seq_info(&si); CHECK(si.mode == SEQ_STEP && si.write, "writing steps");
    key(0, 60);
    CHECK(st.steps[0][0] == 49 && st.steps[0][1] == 53 && st.steps[0][2] == 56, "the step holds the chord");
    fresh(); pads_on(0); st.ip[st.inst].arp[0] = ARP_RIFF;
    tap(H1_BTN_ARP); tap(H1_BTN_REC); key(0, 80); tap(H1_BTN_REC);
    CHECK(st.riff_len == 3, "RIFF takes the notes one by one, as with chord memory");
}

static void smooth(void)
{
    static const int C[3] = {48, 52, 55}, F[3] = {48, 53, 57}, AM[3] = {48, 52, 57}, UP[3] = {60, 64, 67};
    int nt[3] = {53, 57, 60}, first[3] = {48, 52, 55};
    ch_smooth(nt, 3, ch_centre(first, 3, 0), 0);
    CHECK(nt[0] == 48 && nt[1] == 53 && nt[2] == 57, "F after C: second inversion, an octave down");
    fresh(); pads_on(0); st.chord[2] = VOICE_SMOOTH;
    key(0, 60); CHECK(only(C, 3), "the first chord is in root position");
    clear(); key(6, 60); CHECK(only(F, 3), "F stays close to C");
    clear(); key(4, 60); CHECK(only(AM, 3), "A minor stays close to F");
    fresh(); pads_on(0); st.chord[2] = VOICE_SMOOTH;
    key(0, 60); tap(H1_BTN_OCTUP); clear(); key(0, 60);
    CHECK(only(UP, 3), "OCT + still moves it up");
}

static void saved(void)
{
    static uint8_t buf[sizeof(H1State)];
    uint32_t size;
    fresh(); pads_on(10);
    { const void *s = h1_state(&size); memcpy(buf, s, size); }
    fresh(); CHECK(h1_state_load(buf, size) == 0 && st.chord[0] == CHORD_PADS && st.rsv[1] == 10, "mode and set kept");
    buf[offsetof(H1State, chord)] = 200; buf[offsetof(H1State, rsv) + 1] = 200;
    CHECK(h1_state_load(buf, size) == 0 && st.chord[0] == CHORD_OFF && st.rsv[1] == 0, "nonsense clamped");
    buf[offsetof(H1State, chord) + 2] = VOICE_SMOOTH;
    CHECK(h1_state_load(buf, size) == 0 && st.chord[2] == VOICE_SMOOTH, "SMOOTH is kept");
    buf[offsetof(H1State, chord) + 2] = 200;
    CHECK(h1_state_load(buf, size) == 0 && st.chord[2] == 0, "a voicing past SMOOTH is CLOSE");
}

static void hold(int b) { h1_button(b, 1); go(700); h1_button(b, 0); go(60); }
static int col(int k, const char *name, const char *text)
{
    const char *n = "", *t = "";
    h1_control(k, 0, 0, &n, &t);
    return !strcmp(n, name) && (!text || !strcmp(t, text));
}
static void turn(int enc, int d) { h1_encoder(enc, d); go(60); }
static void edit_glo(void)
{
    h1_button(H1_BTN_EDIT, 1); go(30); tap(H1_BTN_GLO); h1_button(H1_BTN_EDIT, 0); go(60);
}

static void page_controls(void)
{
    fresh();
    tap(H1_BTN_ARP); CHECK(h1_page() == PG_ARP, "tap ARP: the SEQ page");
    hold(H1_BTN_ARP); CHECK(h1_page() == PG_CHORD, "hold ARP: CHORDS, from the SEQ page too");
    hold(H1_BTN_ARP); CHECK(h1_page() == PG_CHORD && col(H1_ENC_K1, "MODE", "OFF"), "held again: it stays");
    CHECK(col(H1_ENC_K2, "", 0) && col(H1_ENC_SELECT, "SUBPAGE", "1 PADS"), "OFF has no second column");
    turn(H1_ENC_K1, 1); CHECK(st.chord[0] == CHORD_PADS && col(H1_ENC_K1, "MODE", "PADS"), "PADS is next to OFF");
    turn(H1_ENC_K2, 4); CHECK(st.rsv[1] == 4 && col(H1_ENC_K2, "SET", "BALLAD 2"), "K2 picks the set");
    turn(H1_ENC_K1, 1); CHECK(st.chord[0] == CHORD_MEMORY && col(H1_ENC_K2, "TYPE", "MEM"), "MEMORY");
    turn(H1_ENC_K1, 2); turn(H1_ENC_K2, 7);
    CHECK(st.chord[0] == CHORD_SHAPE && st.chord[1] == 7, "SHAPE's type");
    turn(H1_ENC_K1, -1); CHECK(st.chord[0] == CHORD_SCALE && st.chord[1] < CHORD_NSCALE, "kept in SCALE's range");
    turn(H1_ENC_K1, -2); CHECK(st.chord[0] == CHORD_PADS && st.rsv[1] == 4, "the set is kept");
    edit_glo(); CHECK(st.chord[0] == CHORD_OFF, "EDIT + GLO: off");
    edit_glo(); CHECK(st.chord[0] == CHORD_PADS, "and PADS again");
    h1_key(0, 1); h1_key(2, 1); go(60); edit_glo(); h1_key(0, 0); h1_key(2, 0); go(200);
    CHECK(st.chord[0] == CHORD_OFF && st.chord_n == 3 && st.chord_mem[1] == 4, "pad keys are not captured");
    turn(H1_ENC_SELECT, 1);
    CHECK(col(H1_ENC_SELECT, "SUBPAGE", "2 PLAY") && col(H1_ENC_K1, "VOICING", 0) && col(H1_ENC_K3, "", 0), "PLAY");
    turn(H1_ENC_K1, 9); CHECK(st.chord[2] == VOICE_SMOOTH && col(H1_ENC_K1, "VOICING", "SMOOTH"), "to SMOOTH");
    CHECK(col(H1_ENC_K2, "STRUM", 0), "STRUM");
    tap(H1_BTN_HOME); CHECK(h1_page() == PG_HOME, "HOME leaves");
    h1_button(H1_BTN_EDIT, 1); go(30); hold(H1_BTN_ARP); h1_button(H1_BTN_EDIT, 0); go(60);
    CHECK(h1_page() == PG_HOME && st.keys[3] == 1, "EDIT + ARP is still HOLD, and opens no page");
    fresh(); tap(H1_BTN_GLO); turn(H1_ENC_SELECT, 1); turn(H1_ENC_SELECT, 1);
    CHECK(col(H1_ENC_K1, "TEMPO", 0), "GLO has two subpages");
}

int main(void)
{
    sets();
    play();
    latch();
    sequencers();
    smooth();
    saved();
    page_controls();
    printf("pads: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
