/* KEY FOLLOW's data, on the core alone. Run by tests/run_tests.sh.
 *
 *   recorded notes   a take's notes carry GLO's key (ROOT and SCALE as they were), an off its on's, and the blob's
 *                    header says so
 *   blob             saved, loaded and saved again: the same bytes; keys the header does not vouch for, or that
 *                    are not keys, load as none
 *   state            the step pattern's key and each track's KEY bit: kept when valid, none when not
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
static int rt_n[8], rt_seq[64], rt_at[64], nrt; /* real time: counts by status - 0xF8, and Start / Stop in order,
                                                  * with how many other messages had gone out before each */
static void rt_reset(void) { memset(rt_n, 0, sizeof rt_n); nrt = 0; }
static int f8_at_start;                          /* the clock count when the last Start went out */
static int stamp[64], nstamp, stamping;         /* the clock count at each note-on of note 60, while stamping */
static void mo_fn(uint8_t s, uint8_t a, uint8_t b)
{
    if (stamping && s == 0x90 && a == 60 && b && nstamp < 64)
        stamp[nstamp++] = rt_n[0];
    if (s >= 0xF8) {
        rt_n[s - 0xF8]++;
        if (s == 0xFA)
            f8_at_start = rt_n[0];
        if (s != 0xF8 && nrt < 64) {
            rt_at[nrt] = nmo;
            rt_seq[nrt++] = s;
        }
        return;
    }
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
static int first_on(int from)                    /* where the first note-on at or after FROM is in mo[], or -1 */
{
    int i;
    for (i = from; i < nmo; i++)
        if ((mo[i][0] & 0xF0) == 0x90 && mo[i][2])
            return i;
    return -1;
}
static int exact(void)                           /* every channel's every note: exactly as many offs as ons (no
                                                  * stray off on a channel, which would cut another note there) */
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
            if (open[c][k]) {
                printf("  channel %d note %d: %+d ons over offs\n", c + 1, k, open[c][k]);
                return 0;
            }
    return 1;
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
    rt_reset();
    h1_midi_out(mo_fn);
    go(100);
}


#define LEV_HEAD 12
static int is_key(uint8_t k, int root, int scale) { return k == key_byte(root, scale); }

static void recorded_notes_carry_key(void)
{
    const uint8_t *p;
    uint32_t size, i, ne, seen = 0, ok = 1;
    fresh();
    st.keys[0] = 2;                              /* ROOT D */
    st.keys[1] = 2;                              /* SCALE MINOR */
    tap(H1_BTN_REC);
    note(7, 150);
    note(9, 150);
    tap(H1_BTN_REC);
    p = (const uint8_t *)h1_loop(&size);
    CHECK(p && size >= LEV_HEAD + 32u, "the take is in the blob");
    CHECK(p[5] & 1, "the header says the events carry keys");
    ne = p[6] | p[7] << 8;
    for (i = 0; i < ne; i++) {
        const uint8_t *e = p + LEV_HEAD + i * 8u;
        if ((e[3] & 7) == 1 || (e[3] & 7) == 0) {
            seen++;
            if (!is_key(e[7], 2, 2)) ok = 0;
        }
    }
    CHECK(seen == 4, "two ons and two offs");
    CHECK(ok, "each note carries D minor");
}

static void off_takes_its_ons_key(void)
{
    const uint8_t *p;
    uint32_t size, i, ne, ok = 1, seen = 0;
    fresh();
    st.keys[0] = 2;
    st.keys[1] = 2;
    tap(H1_BTN_REC);
    h1_key(7, 1);
    go(150);
    st.keys[0] = 9;                              /* ROOT turned while the key is held */
    st.keys[1] = 5;
    h1_key(7, 0);
    go(60);
    note(9, 150);                                /* this one is in the new key */
    tap(H1_BTN_REC);
    p = (const uint8_t *)h1_loop(&size);
    ne = p[6] | p[7] << 8;
    for (i = 0; i < ne; i++) {
        const uint8_t *e = p + LEV_HEAD + i * 8u;
        int n = e[2];
        if ((e[3] & 7) > 1) continue;
        seen++;
        if (!is_key(e[7], n == 60 ? 2 : 9, n == 60 ? 2 : 5)) ok = 0;
    }
    CHECK(seen == 4 && ok, "an off has its on's key, though ROOT moved in between");
}

static void arp_notes_carry_key(void)
{
    const uint8_t *p;
    uint32_t size, i, ne, seen = 0, ok = 1;
    fresh();
    st.keys[0] = 5;
    st.keys[1] = 3;
    st.ip[st.inst].arp[0] = ARP_UP;
    tap(H1_BTN_REC);
    h1_key(7, 1);
    h1_key(11, 1);
    go(800);
    h1_key(7, 0);
    h1_key(11, 0);
    go(200);
    tap(H1_BTN_REC);
    p = (const uint8_t *)h1_loop(&size);
    ne = p[6] | p[7] << 8;
    for (i = 0; i < ne; i++) {
        const uint8_t *e = p + LEV_HEAD + i * 8u;
        if ((e[3] & 7) > 1) continue;
        seen++;
        if (!is_key(e[7], 5, 3)) ok = 0;
    }
    CHECK(seen > 2 && ok, "the arpeggiator's notes carry the key too");
}

static uint8_t blob[LEV_HEAD + 8 * 4];
static void mkblob(uint8_t flag, const uint8_t *keys)
{
    int i;
    memset(blob, 0, sizeof blob);
    blob[0] = 'A'; blob[1] = '2'; blob[2] = '2'; blob[3] = 'T';
    blob[4] = 1; blob[5] = flag;
    blob[6] = 4;
    blob[8] = 0x00; blob[9] = 0x06;              /* 1536 ticks */
    for (i = 0; i < 4; i++) {
        uint8_t *e = blob + LEV_HEAD + i * 8;
        e[0] = (uint8_t)(i * 20);
        e[2] = 60;
        e[3] = (uint8_t)(i & 1 ? 0 : 1);
        e[5] = 0;
        e[6] = 90;
        e[7] = keys[i];
    }
}
static void blob_round_trip(void)
{
    const uint8_t *p, *q;
    static uint8_t a[4096];
    uint32_t n, m;
    const uint8_t k[4] = {0x25, key_byte(4, 1), key_byte(0, 7), 0x8C};
    fresh();
    mkblob(1, k);
    CHECK(h1_loop_load(blob, sizeof blob) == 0, "a blob with keys loads");
    p = (const uint8_t *)h1_loop(&n);
    memcpy(a, p, n);
    CHECK(a[5] & 1, "it saves with the header bit");
    CHECK(a[LEV_HEAD + 7] == 0 && a[LEV_HEAD + 15] == key_byte(4, 1) && a[LEV_HEAD + 23] == key_byte(0, 7) &&
          a[LEV_HEAD + 31] == 0, "valid keys kept; no valid bit, or root 12, dropped");
    fresh();
    CHECK(h1_loop_load(a, n) == 0, "and loads again");
    q = (const uint8_t *)h1_loop(&m);
    CHECK(m == n && !memcmp(q, a, n), "saved, loaded and saved again: the same bytes");
}
static void junk_keys_dropped(void)
{
    const uint8_t *p;
    uint32_t n, i, ok = 1;
    const uint8_t k[4] = {0xA5, key_byte(4, 1), 0xFF, 0x91};
    fresh();
    mkblob(0, k);
    CHECK(h1_loop_load(blob, sizeof blob) == 0, "a blob without the header bit loads");
    p = (const uint8_t *)h1_loop(&n);
    for (i = 0; i < 4; i++)
        if (p[LEV_HEAD + i * 8 + 7]) ok = 0;
    CHECK(ok, "with every key 0, whatever its events held");
}

static void state_rsv_valid(void)
{
    static uint8_t a[4096];
    const void *p;
    uint32_t n;
    fresh();
    st.rsv[0] = key_byte(4, 1);
    st.trk[2].rsv = 1;
    p = h1_state(&n);
    memcpy(a, p, n);
    fresh();
    CHECK(h1_state_load(a, n) == 0, "the state loads");
    CHECK(st.rsv[0] == key_byte(4, 1) && st.trk[2].rsv == 1 && st.rsv[1] == 0, "a valid key and a KEY bit are kept");
    ((H1State *)a)->rsv[0] = 0x8C;
    ((H1State *)a)->rsv[1] = 0x55;
    ((H1State *)a)->trk[2].rsv = 0xFE;
    ((H1State *)a)->trk[1].rsv = 0xFF;
    CHECK(h1_state_load(a, n) == 0, "a state with junk there loads");
    CHECK(st.rsv[0] == 0 && st.rsv[1] == 0 && st.trk[2].rsv == 0 && st.trk[1].rsv == 1,
          "root 12 is no key; the KEY bit is bit 0 alone");
    ((H1State *)a)->rsv[0] = 0x25;
    h1_state_load(a, n);
    CHECK(st.rsv[0] == 0, "no valid bit, no key");
    fresh();
    CHECK(st.rsv[0] == 0 && st.rsv[1] == 0 && st.trk[0].rsv == 0 && st.trk[3].rsv == 0, "defaults: none");
}

/* ---- the tape plays in the current key ---- */
static int gated(void)                           /* voices the tape holds open */
{
    int v, n = 0;
    for (v = 0; v < NVOICE; v++)
        n += voices[v].stage && voices[v].gate;
    return n;
}
static void take_cmaj(void)                      /* C-E-G in C major on T1, the tape stopped */
{
    fresh();
    st.keys[0] = 0;
    st.keys[1] = 1;
    tap(H1_BTN_REC);
    note(7, 150);
    note(11, 150);
    note(14, 150);
    tap(H1_BTN_REC);
    tap(H1_BTN_PLAY);
    go(200);
}
static void pass(void)                           /* a pass of the tape, then stopped */
{
    nmo = 0;
    tap(H1_BTN_PLAY);
    go(9000);
    tap(H1_BTN_PLAY);
    go(300);
}
static int only_ch1(int a, int b, int c)         /* channel 1 sent these three notes and no others */
{
    int i;
    for (i = 0; i < nmo; i++) {
        int n = mo[i][1];
        if ((mo[i][0] & 0xF0) != 0x90 && (mo[i][0] & 0xF0) != 0x80) continue;
        if ((mo[i][0] & 15) != 0 || (n != a && n != b && n != c)) return 0;
    }
    return mo_n(0x90, a) > 0 && mo_n(0x90, b) > 0 && mo_n(0x90, c) > 0;
}

static void key_off_unchanged(void)
{
    take_cmaj();
    st.keys[0] = 2;
    pass();
    CHECK(only_ch1(60, 64, 67), "KEY off: the take plays as recorded, whatever ROOT is now");
    CHECK(exact() && gated() == 0, "with its offs");
}

static void key_on_follows(void)
{
    take_cmaj();
    st.trk[0].rsv = 1;
    st.keys[0] = 2;
    pass();
    CHECK(only_ch1(62, 66, 69), "KEY on, ROOT D: D F# A");
    CHECK(exact() && gated() == 0, "ended note for note");
    st.keys[1] = 2;                              /* D minor */
    pass();
    CHECK(only_ch1(62, 65, 69), "SCALE MINOR: D F A");
    CHECK(exact() && gated() == 0, "ended note for note");
}

static void drums_stay(void)
{
    const char *tx = 0, *nm = 0;
    fresh();
    h1_control_set(H1_ENC_PRESET, INST_KIT);
    go(100);
    st.keys[0] = 0;
    st.keys[1] = 1;
    tap(H1_BTN_REC);
    note(7, 150);
    note(11, 150);
    note(14, 150);
    tap(H1_BTN_REC);
    tap(H1_BTN_PLAY);
    go(200);
    st.trk[0].rsv = 1;
    st.keys[0] = 2;
    pass();
    CHECK(only_ch1(60, 64, 67), "a KIT take stays put with KEY on");
    CHECK(exact() && gated() == 0, "ended note for note");
    tap(H1_BTN_SEL);
    h1_encoder(H1_ENC_SELECT, 1);
    h1_encoder(H1_ENC_SELECT, 1);
    h1_encoder(H1_ENC_SELECT, 1);
    h1_encoder(H1_ENC_SELECT, 1);
    h1_control(H1_ENC_K1, 0, 0, &nm, &tx);
    CHECK(!strcmp(nm, "T1") && !strcmp(tx, "DRUM"), "the MIX KEY column of a drum track reads DRUM");
    h1_encoder(H1_ENC_K1, 1);
    CHECK(st.trk[0].rsv == 1, "and turning it does nothing");
}

static void old_tape_unchanged(void)
{
    const uint8_t k[4] = {0, 0, 0, 0};
    fresh();
    mkblob(0, k);
    CHECK(h1_loop_load(blob, sizeof blob) == 0, "an old blob loads");
    st.trk[0].rsv = 1;
    st.keys[0] = 2;
    nmo = 0;
    tap(H1_BTN_PLAY);
    go(9000);
    tap(H1_BTN_PLAY);
    go(300);
    CHECK(mo_n(0x90, 60) > 0 && mo_n(0x90, -1) == mo_n(0x90, 60), "a tape without keys plays as recorded");
    CHECK(exact() && gated() == 0, "ended note for note");
}

static void long_note_take(void)                 /* one long C, in C major, the tape stopped */
{
    fresh();
    st.keys[0] = 0;
    st.keys[1] = 1;
    tap(H1_BTN_REC);
    h1_key(7, 1);
    go(2500);
    h1_key(7, 0);
    go(60);
    tap(H1_BTN_REC);
    tap(H1_BTN_PLAY);
    go(200);
}
static void wait_on(void)                        /* play until the note starts */
{
    int i;
    nmo = 0;
    tap(H1_BTN_PLAY);
    for (i = 0; i < 400 && !mo_n(0x90, -1); i++)
        go(20);
}

static void root_change_mid_note(void)
{
    long_note_take();
    st.trk[0].rsv = 1;
    st.keys[0] = 2;
    wait_on();
    CHECK(mo_n(0x90, 62) == 1, "the note sounds in D");
    st.keys[0] = 5;                              /* ROOT moves while it sounds */
    go(3500);
    CHECK(mo_n(0x80, 62) == 1 && mo_n(0x80, -1) == 1, "its off is the note that sounded");
    CHECK(exact() && gated() == 0, "no voice left gated, no stray off");
    tap(H1_BTN_PLAY);
    go(300);
}

static void key_on_mid_note(void)
{
    long_note_take();
    st.keys[0] = 2;
    wait_on();
    CHECK(mo_n(0x90, 60) == 1, "KEY off: it sounds as recorded");
    st.trk[0].rsv = 1;                           /* KEY turned on while it sounds */
    go(3500);
    CHECK(mo_n(0x80, 60) == 1 && mo_n(0x80, -1) == 1, "it ends on its own pitch");
    CHECK(exact() && gated() == 0, "no voice left gated");
    tap(H1_BTN_PLAY);
    go(300);
}

static void stop_wrap_cut_clear_mid_note(void)
{
    int w;
    for (w = 0; w < 3; w++) {
        long_note_take();
        st.trk[0].rsv = 1;
        st.keys[0] = 2;
        wait_on();
        CHECK(mo_n(0x90, 62) == 1, "sounding in D");
        st.keys[0] = 7;                          /* ROOT changed mid-note */
        go(100);
        if (w == 0) tap(H1_BTN_PLAY);            /* stop */
        else if (w == 1) eng_post(EV_CUT, 0);    /* cut */
        else eng_post(EV_CLEAR, 1);              /* clear the track */
        go(400);
        CHECK(mo_n(0x80, 62) == 1 && mo_n(0x80, -1) == 1, "its off is the note that sounded");
        CHECK(exact() && gated() == 0, "no stray off, no voice left gated");
        if (w) { tap(H1_BTN_PLAY); go(300); }
    }
    /* the wrap: a pass of the tape, ROOT changed between its notes */
    take_cmaj();
    st.trk[0].rsv = 1;
    st.keys[0] = 2;
    nmo = 0;
    tap(H1_BTN_PLAY);
    go(2000);
    st.keys[0] = 9;
    go(16000);                                   /* round the loop at least once more */
    tap(H1_BTN_PLAY);
    go(300);
    CHECK(exact() && gated() == 0, "the wrap: ended note for note");
}

static void two_keys_overdub(void)
{
    uint8_t c = key_byte(0, 1), e = key_byte(4, 1), g = key_byte(7, 1);
    take_cmaj();
    st.keys[0] = 4;
    tap(H1_BTN_REC);                             /* an overdub in E major */
    note(11, 150);                               /* 64 */
    note(16, 150);                               /* 69 */
    tap(H1_BTN_REC);
    tap(H1_BTN_PLAY);
    go(200);
    st.trk[0].rsv = 1;
    st.keys[0] = 7;
    pass();
    CHECK(mo_n(0x90, key_map(60, c, g)) > 0 && mo_n(0x90, key_map(67, c, g)) > 0, "the C major notes move from C");
    CHECK(mo_n(0x90, key_map(64, e, g)) > 0 && mo_n(0x90, key_map(69, e, g)) > 0, "the E major notes move from E");
    CHECK(key_map(64, c, g) != key_map(64, e, g), "(the two keys really differ)");
    CHECK(exact() && gated() == 0, "ended note for note");
}

static void shared_pitch(void)
{
    int i;
    fresh();
    st.keys[0] = 0;
    st.keys[1] = 1;
    tap(H1_BTN_REC);
    h1_key(11, 1);                               /* E */
    go(100);
    h1_key(12, 1);                               /* F, which becomes E too */
    go(500);
    h1_key(11, 0);
    go(600);
    h1_key(12, 0);
    go(60);
    tap(H1_BTN_REC);
    tap(H1_BTN_PLAY);
    go(200);
    st.trk[0].rsv = 1;
    st.keys[1] = 3;                              /* C pentatonic */
    nmo = 0;
    tap(H1_BTN_PLAY);
    for (i = 0; i < 400 && !mo_n(0x90, -1); i++)
        go(20);
    CHECK(mo_n(0x90, 64) == 1 && mo_n(0x90, -1) == 1, "two notes on one pitch: one note-on");
    go(900);                                     /* past the first off (+600), before the second (+1200) */
    CHECK(mo_n(0x80, 64) == 0 && gated() == 1, "the first off does not end it");
    go(700);
    CHECK(mo_n(0x80, 64) == 1 && mo_n(0x90, -1) == 1, "the later off does, once");
    CHECK(exact() && gated() == 0, "no voice left gated");
    tap(H1_BTN_PLAY);
    go(300);
}

/* two overlapping tape notes on one pitch, one track: E on at 0, E again at +400 (a second take over the first),
 * the offs at +800 and +1200 */
static void overlap_take(int key2)
{
    static uint8_t b[LEV_HEAD + 8 * 4];
    int i;
    const uint16_t ticks[4] = {0, 200, 400, 600}; /* (about a tick a millisecond) */
    const uint8_t kinds[4] = {1, 1, 0, 0};
    fresh();
    memset(b, 0, sizeof b);
    b[0] = 'A'; b[1] = '2'; b[2] = '2'; b[3] = 'T';
    b[4] = 1; b[5] = 1; b[6] = 4;
    b[8] = 0x00; b[9] = 0x06;
    for (i = 0; i < 4; i++) {
        uint8_t *e = b + LEV_HEAD + i * 8;
        e[0] = (uint8_t)(ticks[i] & 255); e[1] = (uint8_t)(ticks[i] >> 8);
        e[2] = 60;
        e[3] = kinds[i];
        e[6] = 90;
        e[7] = key_byte(0, 1);
    }
    (void)key2;
    CHECK(h1_loop_load(b, sizeof b) == 0, "the overlapping take loads");
}
static int max_gated;
static void watch(int msec)
{
    int i;
    for (i = 0; i < msec / 20; i++) {
        int g = gated();
        go(20);
        if (g > max_gated) max_gated = g;
    }
}
static void on_again(void)
{
    int pass_;
    for (pass_ = 0; pass_ < 2; pass_++) {
        overlap_take(0);
        st.trk[0].rsv = pass_;                   /* KEY off, then on */
        st.keys[0] = 0;
        st.keys[1] = 1;
        nmo = 0;
        max_gated = 0;
        tap(H1_BTN_PLAY);
        if (pass_) {                             /* ROOT moves between the two ons */
            watch(100);
            st.keys[0] = 2;
        }
        watch(5000);
        tap(H1_BTN_PLAY);
        go(300);
        CHECK(exact() && gated() == 0, pass_ ? "KEY on, ROOT between the ons: every note ends"
                                              : "KEY off: every note ends, none left gated");
        if (!pass_)
            CHECK(max_gated == 1, "KEY off: the second on starts the voice again in place (one voice)");
        else
            CHECK(mo_n(0x90, 60) >= 1 && mo_n(0x90, 62) >= 1, "KEY on: both pitches sounded");
    }
}

static void key_subpage(void)
{
    const char *tx = 0, *nm = 0;
    fresh();
    tap(H1_BTN_SEL);
    h1_encoder(H1_ENC_SELECT, 1);
    h1_encoder(H1_ENC_SELECT, 1);
    h1_encoder(H1_ENC_SELECT, 1);
    h1_encoder(H1_ENC_SELECT, 1);
    CHECK(ui_subpage() == 4, "SEL, SELECT four times: MIX's fifth subpage");
    h1_control(H1_ENC_SELECT, 0, 0, &nm, &tx);
    CHECK(strstr(tx, "KEY") != 0, "the screen reader names it KEY");
    h1_control(H1_ENC_K2, 0, 0, &nm, &tx);
    CHECK(!strcmp(nm, "T2") && !strcmp(tx, "OFF"), "K2 is T2, OFF");
    h1_encoder(H1_ENC_K2, 1);
    CHECK(st.trk[1].rsv == 1, "K2 turns T2's KEY on");
    h1_control(H1_ENC_K2, 0, 0, &nm, &tx);
    CHECK(!strcmp(tx, "ON"), "and it reads ON");
    h1_encoder(H1_ENC_K2, 1);
    CHECK(st.trk[1].rsv == 1, "ON is the top");
}

/* ---- the step pattern's key ---- */
static int ons(int n)                            /* note-ons of N on any channel */
{
    int c, k = 0;
    for (c = 0; c < 16; c++) k += mo_n((uint8_t)(0x90 | c), n);
    return k;
}
static void to_step(void)
{
    h1_button(H1_BTN_SEQ, 1); go(800); h1_button(H1_BTN_SEQ, 0); go(50);
}
static void write_step(int key)                  /* REC, one key, REC: a step at the cursor (which moves on) */
{
    tap(H1_BTN_REC);
    note(key, 60);
    tap(H1_BTN_REC);
}
static void play_steps(void)                     /* one pass of the pattern, notes out collected */
{
    nmo = 0;
    tap(H1_BTN_PLAY);
    go(700);
    tap(H1_BTN_PLAY);
    go(300);
}

static void step_key_set_on_first_write(void)
{
    fresh();
    to_step();
    st.keys[0] = 4;                              /* ROOT E */
    st.keys[1] = 2;                              /* MINOR */
    CHECK(st.rsv[0] == 0, "an empty pattern has no key");
    write_step(7);
    CHECK(st.rsv[0] == key_byte(4, 2), "the first step sets the pattern's key to GLO's");
}

static void step_follows(void)
{
    fresh();
    to_step();
    st.trk[0].rsv = 1;
    st.keys[0] = 0; st.keys[1] = 1;
    write_step(7);                               /* C (60) */
    CHECK(st.steps[0][0] == 61, "written in the pattern's own key: as played");
    play_steps();
    CHECK(ons(60) >= 1 && exact(), "KEY on: plays C in C major");
    st.keys[0] = 2;                              /* ROOT D */
    play_steps();
    CHECK(ons(62) >= 1 && ons(60) == 0 && exact(), "ROOT moves to D: the step's note moves to D");
}

static void step_written_in_other_key_stored_mapped(void)
{
    fresh();
    to_step();
    st.trk[0].rsv = 1;
    st.keys[0] = 0; st.keys[1] = 1;
    write_step(7);                               /* the pattern's key: C major */
    st.keys[0] = 2;                              /* ROOT D */
    write_step(9);                               /* D (62), into step 1 */
    CHECK(st.rsv[0] == key_byte(0, 1), "the pattern keeps its key");
    CHECK(st.steps[1][0] == 61, "D played in D major is stored as C (60)");
    st.keys[0] = 0;
    play_steps();
    CHECK(ons(60) >= 1 && ons(62) == 0 && exact(), "back in C major it plays 60");
    st.keys[0] = 2;
    play_steps();
    CHECK(ons(62) >= 1 && ons(60) == 0 && exact(), "in D it plays 62");
}

static void step_key_off_stored_as_played(void)
{
    fresh();
    to_step();
    st.trk[0].rsv = 0;
    st.keys[0] = 0; st.keys[1] = 1;
    write_step(7);
    st.keys[0] = 2;
    write_step(9);
    CHECK(st.steps[1][0] == 63, "KEY off: stored as played (62)");
    st.keys[0] = 5;
    play_steps();
    CHECK(ons(62) >= 1 && ons(60) >= 1 && exact(), "KEY off: plays as stored whatever ROOT is");
}

static void step_mapping_uses_step_track(void)
{
    fresh();
    to_step();
    st.trk[1].rsv = 1;
    st.keys[0] = 0; st.keys[1] = 1;
    track(1);                                    /* T2, KEY on */
    write_step(7);                               /* C (60) */
    CHECK(st.step_trk[0][0] == 1, "the step is on T2");
    track(-1);                                   /* T1, KEY off, in hand */
    st.keys[0] = 5;                              /* ROOT F */
    play_steps();
    CHECK(ons(65) >= 1 && ons(60) == 0 && exact(), "T2's step moves with ROOT though T1 is in hand");
    st.keys[0] = 0;
    write_step(9);                               /* on T1, KEY off: as played (62), into step 1 */
    CHECK(st.steps[1][0] == 63 && st.step_trk[1][0] == 0, "a step on T1 (KEY off) is stored as played");
    st.keys[0] = 5;
    play_steps();
    CHECK(ons(62) >= 1 && ons(65) >= 1 && exact(), "T1's stays as written, T2's moved");
}

static void step_from_060(void)
{
    fresh();
    to_step();
    st.steps[3][0] = 61;
    st.step_trk[3][0] = 0;
    st.step_vel[3][0] = 100;
    st.rsv[0] = 0;
    st.keys[0] = 2; st.keys[1] = 1;
    write_step(9);
    CHECK(st.rsv[0] == key_byte(2, 1), "the next write sets the key");
    CHECK(st.steps[3][0] == 61, "the earlier step is untouched, and reads as in that key");
}

static void clear_all_clears_key(void)
{
    fresh();
    to_step();
    st.keys[0] = 0; st.keys[1] = 1;
    write_step(7);
    CHECK(st.rsv[0] != 0, "a key is set");
    h1_button(H1_BTN_REC, 1); go(3300); h1_button(H1_BTN_REC, 0); go(100);
    CHECK(st.rsv[0] != 0 && st.steps[0][0] == 0, "clearing one track keeps the key");
    tap(H1_BTN_REC);
    CHECK(st.rsv[0] == 0, "CLEAR ALL clears the key");
}

int main(void)
{
    recorded_notes_carry_key();
    off_takes_its_ons_key();
    arp_notes_carry_key();
    blob_round_trip();
    junk_keys_dropped();
    state_rsv_valid();
    key_off_unchanged();
    key_on_follows();
    drums_stay();
    old_tape_unchanged();
    root_change_mid_note();
    key_on_mid_note();
    stop_wrap_cut_clear_mid_note();
    two_keys_overdub();
    shared_pitch();
    on_again();
    key_subpage();
    step_key_set_on_first_write();
    step_follows();
    step_written_in_other_key_stored_mapped();
    step_key_off_stored_as_played();
    step_mapping_uses_step_track();
    step_from_060();
    clear_all_clears_key();
    h1_midi_out(0);
    printf("keyfollow: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
