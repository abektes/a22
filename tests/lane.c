/* The key lane (LANE), on the core alone. Run by tests/run_tests.sh.
 *
 *   entries      set through EV_LANE, one a bar; "—" on all three fields is no entry; kept in the tape's tail at
 *                tick + 15,360 (LEV_GLOBAL) through a save and a load, CUT, PASTE and clearing a track; gone with
 *                CLEAR ALL; kept past BARS turned down; a tape with only entries overdubs
 *   key in force each field from the latest entry at or before the bar that sets it, GLO's (or DEG I) before any,
 *                no wrap; GLO's ROOT and SCALE fill in when they change; METER moves which bar an entry is in
 *   following    KEY-on tracks follow bar by bar (I-IV-V-vi on MIDI out), DEG does nothing in CHROM, KEY-off
 *                tracks and STEP mode ignore the lane, a note across a bar change ends on its own pitch
 *   recording    a note played over DEG IV on a KEY-on track is stored three steps down, in the bar's key
 *   the page     TAPE's third subpage LANE (BAR ROOT SCALE DEG): the knobs set entries, BAR clamps to BARS, the cells
 *                are drawn, the deck has a mark and a header, STEP mode has no LANE
 *   live keys    snap to the lane's root and scale while the tape plays, GLO's when stopped; chord SCALE too
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

/* ---- what went out ---- */
static uint8_t mo[4096][3];
static int nmo;
static void mo_fn(uint8_t s, uint8_t a, uint8_t b)
{
    if (s >= 0xF8) return;
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
static int exact(void)                           /* every channel's every note: exactly as many offs as ons */
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
static int gated(void)                           /* voices held open */
{
    int v, n = 0;
    for (v = 0; v < NVOICE; v++)
        n += voices[v].stage && voices[v].gate;
    return n;
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
    st.glo[0] = 240;                             /* (a 4/4 bar: 1 s) */
    st.keys[0] = 0;                              /* GLO: C major */
    st.keys[1] = 1;
    go(100);
}
static void lane(int bar, int field, int value) { eng_post_lane(bar, field, value); go(10); }
#define NONE 31

/* ---- the tape's blob ---- */
#define C_MAJ 0x90                               /* key_byte(0, 1) */
static uint8_t bl[12 + 8 * 64];
static int nbl;
static void b_ev(int tick, int d1, int kind, int d2, int key)
{
    uint8_t *e = bl + 12 + 8 * nbl++;
    memset(e, 0, 8);
    e[0] = (uint8_t)tick; e[1] = (uint8_t)(tick >> 8); e[2] = (uint8_t)d1; e[3] = (uint8_t)kind; e[6] = (uint8_t)d2;
    e[7] = (uint8_t)key;
}
static void b_note(int tick, int len, int n) { b_ev(tick, n, 1, 100, C_MAJ); b_ev(tick + len, n, 0, 0, C_MAJ); }
static int b_load(void)                          /* the events in bl, as a 4-bar 4/4 tape, sorted by tick */
{
    int i, j;
    for (i = 1; i < nbl; i++)                    /* (a stable sort, by tick) */
        for (j = i; j > 0; j--) {
            uint8_t *a = bl + 12 + 8 * (j - 1), *b = a + 8, t[8];
            if ((a[0] | a[1] << 8) <= (b[0] | b[1] << 8)) break;
            memcpy(t, a, 8); memcpy(a, b, 8); memcpy(b, t, 8);
        }
    memset(bl, 0, 12);
    bl[0] = 0x41; bl[1] = 0x32; bl[2] = 0x32; bl[3] = 0x54;   /* "A22T" */
    bl[4] = 1; bl[5] = 1;
    bl[6] = (uint8_t)nbl;
    bl[8] = 0x00; bl[9] = 0x06;                  /* 1536 ticks */
    return h1_loop_load(bl, 12u + 8u * (uint32_t)nbl);
}
static void cegs_each_bar(void)                  /* C-E-G at each of the four bars' first tick, in C major */
{
    int b;
    nbl = 0;
    for (b = 0; b < 4; b++) {
        b_note(b * 384, 200, 60);
        b_note(b * 384, 200, 64);
        b_note(b * 384, 200, 67);
    }
    CHECK(b_load() == 0, "the take loads");
}
static int tblob(int kind, int *tick, int *d1, int *d2, int *flags)   /* the saved tape's events of a kind */
{
    uint32_t size = 0, i, n = 0;
    const uint8_t *b = (const uint8_t *)h1_loop(&size);
    for (i = 0; b && 12u + 8u * (i + 1u) <= size; i++) {
        const uint8_t *e = b + 12u + 8u * i;
        if ((e[3] & 7) != kind) continue;
        if (tick && n < 8) tick[n] = e[0] | e[1] << 8;
        if (d1 && n < 8) d1[n] = e[2];
        if (d2 && n < 8) d2[n] = e[6];
        if (flags && n < 8) flags[n] = e[3] | e[4] << 8;   /* (flags, and the track above) */
        n++;
    }
    return (int)n;
}
static void pass(void)                           /* one pass of a 4-bar tape at 240, then stopped */
{
    nmo = 0;
    tap(H1_BTN_PLAY);
    go(3900);
    tap(H1_BTN_PLAY);
    go(300);
}
static int ons(int *out, int max)                /* channel 1's note-ons, in order */
{
    int i, n = 0;
    for (i = 0; i < nmo && n < max; i++)
        if (mo[i][0] == 0x90 && mo[i][2]) out[n++] = mo[i][1];
    return n;
}
static int triad(const int *o, int a, int b, int c)   /* these three, in any order */
{
    int x[3] = {o[0], o[1], o[2]}, i, j;
    for (i = 1; i < 3; i++)
        for (j = i; j > 0 && x[j - 1] > x[j]; j--) { int t = x[j]; x[j] = x[j - 1]; x[j - 1] = t; }
    return x[0] == a && x[1] == b && x[2] == c;
}

/* ---- entries ---- */
static void lane_set_and_clear(void)
{
    fresh();
    lane(2, 0, 5);
    CHECK(eng_lane_get(2, 0) == 5 && eng_lane_get(2, 1) == -1 && eng_lane_get(2, 2) == -1, "bar 2: ROOT F, the rest —");
    CHECK(eng_lane_get(1, 0) == -1 && eng_lane_get(3, 0) == -1, "no entry on the bars beside it");
    CHECK(tblob(4, 0, 0, 0, 0) == 1, "one LEV_KEY event");
    CHECK(loop_state() == LOOP_IDLE, "the tape is not empty");
    lane(2, 1, 2);
    lane(2, 2, 3);
    CHECK(eng_lane_get(2, 1) == 2 && eng_lane_get(2, 2) == 3 && tblob(4, 0, 0, 0, 0) == 1, "SCALE and DEG: the same entry");
    lane(2, 0, NONE);
    lane(2, 1, NONE);
    CHECK(tblob(4, 0, 0, 0, 0) == 1 && eng_lane_get(2, 2) == 3, "DEG still set: still an entry");
    lane(2, 2, NONE);
    CHECK(tblob(4, 0, 0, 0, 0) == 0, "ROOT, SCALE and DEG all —: no LEV_KEY event left");
    CHECK(eng_lane_get(2, 0) == -1 && eng_lane_get(2, 1) == -1 && eng_lane_get(2, 2) == -1, "and no entry");
    CHECK(loop_state() == LOOP_EMPTY, "the tape is empty again");
    lane(4, 0, NONE);
    CHECK(tblob(4, 0, 0, 0, 0) == 0, "— on a bar without an entry makes none");
}

static void lane_inherits(void)
{
    fresh();
    st.keys[0] = 7;                              /* GLO: G major */
    lane(0, 1, 2);                               /* bar 0: SCALE MINOR, DEG V */
    lane(0, 2, 4);
    lane(2, 0, 5);                               /* bar 2: ROOT F only */
    CHECK(eng_key_at(1) == key_byte(7, 2), "bar 1: GLO's root, the minor scale of bar 0");
    CHECK(eng_key_at(3) == key_byte(5, 2), "bar 3: F, still minor");
    CHECK(eng_deg_at(1) == 4 && eng_deg_at(2) == 4 && eng_deg_at(3) == 4, "ROOT alone keeps DEG too");
}

static void no_wrap(void)
{
    fresh();
    lane(3, 0, 5);
    CHECK(eng_key_at(0) == eng_key_glo() && eng_key_at(2) == eng_key_glo(), "before the first entry: GLO's key");
    CHECK(eng_deg_at(0) == 0, "and DEG I");
    CHECK(eng_key_at(3) == key_byte(5, 1) && eng_key_at(31) == key_byte(5, 1), "from bar 3 on: F major");
    CHECK(eng_key_at(0) != eng_key_at(3), "the last entry does not wrap round to bar 0");
}

/* ---- following ---- */
static void follows_bar_by_bar(void)
{
    int o[64], n;
    fresh();
    cegs_each_bar();
    st.trk[0].rsv = 1;                           /* KEY on */
    lane(1, 2, 3);                               /* I IV V vi */
    lane(2, 2, 4);
    lane(3, 2, 5);
    pass();
    n = ons(o, 64);
    CHECK(n == 12, "twelve notes in four bars");
    if (n == 12) {
        CHECK(triad(o, 60, 64, 67), "bar 1: C E G");
        CHECK(triad(o + 3, 65, 69, 72), "bar 2: F A C");
        CHECK(triad(o + 6, 67, 71, 74), "bar 3: G B D");
        CHECK(triad(o + 9, 69, 72, 76), "bar 4: A C E");
    }
    CHECK(mo_n(0x90, -1) == n && exact() && gated() == 0, "all on channel 1, each ended");
}

static void deg_chrom_nothing(void)
{
    int o[64], n;
    fresh();
    cegs_each_bar();
    st.trk[0].rsv = 1;
    lane(0, 1, 0);                               /* SCALE CHROM, DEG IV */
    lane(0, 2, 3);
    pass();
    n = ons(o, 64);
    CHECK(n == 12 && triad(o, 60, 64, 67) && triad(o + 9, 60, 64, 67), "DEG IV in CHROM: C E G as recorded");
    CHECK(exact() && gated() == 0, "each ended");
}

static void key_off_ignores_lane(void)
{
    int o[64], n;
    fresh();
    cegs_each_bar();
    lane(0, 0, 2);                               /* D minor, IV: KEY is off */
    lane(0, 1, 2);
    lane(0, 2, 3);
    pass();
    n = ons(o, 64);
    CHECK(n == 12 && triad(o, 60, 64, 67) && triad(o + 6, 60, 64, 67), "KEY off: C E G as recorded");
    CHECK(exact() && gated() == 0, "each ended");
}

static void boundary(void)
{
    fresh();
    nbl = 0;
    b_note(0, 600, 60);                          /* C from bar 1 into bar 2 */
    b_note(384, 100, 64);                        /* E on bar 2's first tick */
    CHECK(b_load() == 0, "the take loads");
    st.trk[0].rsv = 1;
    lane(1, 2, 4);                               /* bar 2: DEG V */
    pass();
    CHECK(mo_n(0x90, 60) == 1 && mo_n(0x80, 60) == 1, "the C sounds as C, and ends as C after the bar changed");
    CHECK(mo_n(0x90, 71) == 1 && mo_n(0x90, 64) == 0, "the E on the bar's first tick takes its DEG: B");
    CHECK(exact() && gated() == 0, "nothing left open, no stray off");
}

/* ---- recording ---- */
static int key_ev(int kind, int *d1, int *key)  /* the first event of a kind in the blob: its note and key */
{
    uint32_t size = 0, i;
    const uint8_t *b = (const uint8_t *)h1_loop(&size);
    for (i = 0; b && 12u + 8u * (i + 1u) <= size; i++) {
        const uint8_t *e = b + 12u + 8u * i;
        if ((e[3] & 7) != kind) continue;
        *d1 = e[2];
        *key = e[7];
        return 1;
    }
    return 0;
}
static void record_over_deg(void)
{
    int d1 = -1, k = -1, o[8];
    fresh();
    st.trk[0].rsv = 1;                           /* KEY on */
    lane(0, 2, 3);                               /* bar 1: DEG IV, C major */
    tap(H1_BTN_REC);
    h1_key(12, 1);                               /* F (65) */
    go(200);
    h1_key(12, 0);
    go(60);
    tap(H1_BTN_REC);
    tap(H1_BTN_PLAY);
    go(100);
    CHECK(key_ev(1, &d1, &k) && d1 == 60 && k == C_MAJ, "KEY on over DEG IV: F is stored as C, in C major");
    CHECK(key_ev(0, &d1, &k) && d1 == 60, "its off too");
    pass();
    CHECK(ons(o, 8) == 1 && o[0] == 65, "and it plays back as played: F");
    CHECK(exact() && gated() == 0, "ended");

    fresh();                                     /* KEY off: as played, with the bar's root and scale */
    lane(0, 0, 2);
    lane(0, 1, 2);
    lane(0, 2, 3);
    tap(H1_BTN_REC);
    h1_key(16, 1);                               /* A (69) */
    go(200);
    h1_key(16, 0);
    go(60);
    tap(H1_BTN_REC);
    CHECK(key_ev(1, &d1, &k) && d1 == 69 && k == key_byte(2, 2), "KEY off: A as played, with D minor as its key");

    fresh();                                     /* held from a DEG IV bar into a DEG I bar */
    st.trk[0].rsv = 1;
    lane(0, 2, 3);
    lane(1, 2, 0);
    tap(H1_BTN_REC);
    h1_key(12, 1);                               /* F, let go in bar 2 */
    go(1300);
    h1_key(12, 0);
    go(60);
    tap(H1_BTN_REC);
    CHECK(key_ev(1, &d1, &k) && d1 == 60, "its on is stored as C");
    CHECK(key_ev(0, &d1, &k) && d1 == 60, "and its off, let go in a bar of DEG I, as its on was: C");
}

/* ---- live keys ---- */
static void live_keys_snap_to_lane(void)
{
    fresh();
    lane(0, 0, 2);                               /* bar 1: D minor */
    lane(0, 1, 2);
    CHECK(scale_snap(71) == 71, "stopped: B is in GLO's C major");
    tap(H1_BTN_PLAY);
    CHECK(loop_state() == LOOP_PLAY, "a tape of entries alone plays");
    CHECK(eng_key_live() == key_byte(2, 2), "playing over bar 1: the live key is D minor");
    nmo = 0;
    h1_key(18, 1);                               /* B (71) */
    go(60);
    h1_key(18, 0);
    go(60);
    CHECK(mo_n(0x90, 70) == 1 && mo_n(0x90, 71) == 0, "a pressed B snaps down to Bb in D minor");
    tap(H1_BTN_PLAY);
    CHECK(eng_key_live() == eng_key_glo(), "stopped: GLO's key");
    nmo = 0;
    h1_key(18, 1);
    go(60);
    h1_key(18, 0);
    go(60);
    CHECK(mo_n(0x90, 71) == 1 && mo_n(0x90, 70) == 0, "stopped: B stays B");
}

static void step_mode_ignores_lane(void)
{
    fresh();
    lane(0, 0, 2);                               /* bar 1: ROOT D */
    CHECK(eng_key_play() == key_byte(2, 1), "in tape mode the tape plays bar 1 in D");
    st.seq[0] = SEQ_STEP;
    eng_post(EV_SEQ_MODE, 0);
    go(20);
    st.trk[0].rsv = 1;
    st.steps[0][0] = 61;                         /* a C on step 1, T1, the pattern in C major */
    st.step_trk[0][0] = 0;
    st.step_vel[0][0] = 100;
    st.rsv[0] = C_MAJ;
    CHECK(eng_key_play() == eng_key_glo(), "STEP mode: the pattern plays in GLO's key");
    nmo = 0;
    eng_post(EV_PLAY, 0);
    go(300);
    eng_post(EV_PLAY, 0);
    go(100);
    CHECK(mo_n(0x90, 60) >= 1 && mo_n(0x90, 62) == 0, "the step plays C, not the lane's D");
}

static void chord_scale_follows_lane(void)
{
    fresh();
    st.chord[0] = CHORD_SCALE;
    st.chord[1] = 0;                             /* TRIAD, CLOSE, no strum */
    st.chord[2] = 0;
    st.chord[3] = 0;
    lane(0, 0, 2);                               /* bar 1: D minor */
    lane(0, 1, 2);
    tap(H1_BTN_PLAY);
    nmo = 0;
    h1_key(11, 1);                               /* E (64) */
    go(60);
    h1_key(11, 0);
    go(60);
    CHECK(mo_n(0x90, 64) == 1 && mo_n(0x90, 67) == 1 && mo_n(0x90, 70) == 1 && mo_n(0x90, 71) == 0,
          "playing over D minor: E's chord is E G Bb");
    tap(H1_BTN_PLAY);
    nmo = 0;
    h1_key(11, 1);
    go(60);
    h1_key(11, 0);
    go(60);
    CHECK(mo_n(0x90, 71) == 1 && mo_n(0x90, 70) == 0, "stopped: E G B in C major");
}

static void glo_change_rebuilds(void)
{
    fresh();
    lane(1, 0, 5);                               /* ROOT F, SCALE — */
    CHECK(eng_key_at(1) == key_byte(5, 1), "bar 2: F with GLO's major");
    st.keys[1] = 3;                              /* GLO's SCALE to PENTA */
    st.keys[0] = 9;
    go(20);
    CHECK(eng_key_at(1) == key_byte(5, 3), "GLO's SCALE turned: bar 2 follows it");
    CHECK(eng_key_at(0) == key_byte(9, 3), "and bar 1 is GLO's key");
}

static void meter_change_keeps_tick(void)
{
    int tick[8], d1[8], d2[8];
    fresh();
    lane(3, 0, 5);                               /* bar 4 in 4/4: tick 1,152 */
    CHECK(tblob(4, tick, 0, 0, 0) == 1 && tick[0] == 15360 + 1152, "the entry at bar 4's first tick, in the tail");
    st.seq[2] = 1;                               /* METER 3/4: 288 a bar, tick 1,152 is bar 5's first */
    go(20);
    CHECK(tblob(4, tick, 0, 0, 0) == 1 && tick[0] == 15360 + 1152, "METER moved: the entry keeps its tick");
    CHECK(eng_key_at(3) == eng_key_glo() && eng_key_at(4) == key_byte(5, 1), "and applies from the bar it now falls in");
    CHECK(eng_lane_get(4, 0) == 5 && eng_lane_get(3, 0) == -1, "the lane shows it at that bar");
    lane(4, 1, 2);
    CHECK(tblob(4, tick, d1, d2, 0) == 1 && tick[0] == 15360 + 1152 && d1[0] == 5 && (d2[0] & 15) == 2,
          "setting that bar edits the entry where it is");
    st.seq[2] = 2;                               /* 5/4: 480 a bar, tick 1,152 in bar 3 */
    go(20);
    CHECK(eng_key_at(2) == key_byte(5, 2) && eng_key_at(1) == eng_key_glo(), "5/4: it applies from bar 3");

    fresh();                                     /* two entries in one bar after METER moved: the later tick's */
    st.seq[2] = 1;                               /* 3/4 */
    go(20);
    lane(5, 0, 2);                               /* tick 1,440 */
    lane(6, 0, 5);                               /* tick 1,728 */
    st.seq[2] = 2;                               /* 5/4: both in bar 4 (1,440 to 1,919) */
    go(20);
    CHECK(eng_key_at(3) == key_byte(5, 1) && eng_lane_get(3, 0) == 5, "two in one bar: the later tick's applies");
    lane(3, 1, 2);
    CHECK(tblob(4, tick, d1, d2, 0) == 2 && tick[0] == 15360 + 1440 && tick[1] == 15360 + 1728, "both kept");
    CHECK(d1[0] == 2 && (d2[0] & 15) == 15 && d1[1] == 5 && (d2[1] & 15) == 2,
          "setting that bar edits the later one, and leaves the earlier");
}

static void lane_tail_in_blob(void)
{
    int tick[8], d1[8], d2[8], fl[8], n;
    fresh();
    lane(0, 0, 5);
    lane(2, 1, 2);
    lane(2, 2, 6);
    n = tblob(4, tick, d1, d2, fl);
    CHECK(n == 2, "two LEV_KEY events in h1_loop's blob");
    if (n == 2) {
        CHECK(tick[0] == 15360 && tick[1] == 15360 + 768, "at their bars' first ticks + 15,360");
        CHECK((fl[0] & 0x10) && (fl[1] & 0x10) && (fl[0] >> 8) == 0 && (fl[1] >> 8) == 0, "LEV_GLOBAL set, track 0");
        CHECK(d1[0] == 5 && d2[0] == (15 | 7 << 4), "bar 1: ROOT 5 in d1; SCALE and DEG — (15, 7) in d2");
        CHECK(d1[1] == 127 && d2[1] == (2 | 6 << 4), "bar 3: ROOT — (127); SCALE 2 in bits 0-3, DEG 6 in bits 4-6");
    }
}

static void lane_survives(void)
{
    static uint8_t copy[4096];
    const uint8_t *p;
    uint32_t n, m;
    fresh();
    nbl = 0;
    b_note(0, 200, 60);
    b_note(384, 200, 64);
    CHECK(b_load() == 0, "a take on T1 loads");
    lane(0, 1, 2);
    lane(2, 0, 5);
    lane(2, 2, 4);
    p = (const uint8_t *)h1_loop(&n);
    memcpy(copy, p, n);
    fresh();
    CHECK(eng_lane_get(0, 1) == -1, "a power cycle: no lane");
    CHECK(h1_loop_load(copy, n) == 0, "the saved tape loads");
    CHECK(eng_lane_get(0, 1) == 2 && eng_lane_get(2, 0) == 5 && eng_lane_get(2, 2) == 4, "its entries are back");
    CHECK(eng_key_at(2) == key_byte(5, 2) && eng_deg_at(3) == 4, "and in force");
    p = (const uint8_t *)h1_loop(&m);
    CHECK(m == n && !memcmp(p, copy, n), "saved again: the same bytes");
    eng_post(EV_CUT, 0);
    go(20);
    CHECK(tblob(1, 0, 0, 0, 0) == 0, "CUT took T1's notes");
    CHECK(tblob(4, 0, 0, 0, 0) == 2 && eng_lane_get(2, 0) == 5, "and left the entries (they are no track's)");
    eng_post(EV_PASTE, 0);
    go(20);
    CHECK(tblob(1, 0, 0, 0, 0) == 2 && tblob(4, 0, 0, 0, 0) == 2, "PASTE: the notes back, the entries as they were");
    eng_post(EV_CLEAR, 1);
    go(20);
    CHECK(tblob(1, 0, 0, 0, 0) == 0 && tblob(4, 0, 0, 0, 0) == 2 && eng_key_at(2) == key_byte(5, 2),
          "clearing T1 keeps the entries");
    CHECK(eng_seq_has(0) == 0, "and T1 counts as empty");
    eng_post(EV_CLEAR, 0);
    go(20);
    CHECK(tblob(4, 0, 0, 0, 0) == 0 && eng_lane_get(2, 0) == -1 && eng_key_at(2) == eng_key_glo(), "CLEAR ALL: gone");
    CHECK(loop_state() == LOOP_EMPTY, "the tape is empty");
}

static void lane_only_tape_overdubs(void)
{
    SeqInfo si;
    uint32_t n;
    const uint8_t *p;
    fresh();
    lane(1, 0, 5);
    tap(H1_BTN_REC);
    h1_key(7, 1);
    go(100);
    eng_seq_info(&si);
    CHECK(si.loop_state == LOOP_DUB, "a tape with only entries: the take overdubs");
    CHECK(si.loop_pos < 0.05f, "from IN");
    h1_key(7, 0);
    go(60);
    tap(H1_BTN_REC);
    CHECK(tblob(4, 0, 0, 0, 0) == 1 && eng_lane_get(1, 0) == 5, "the entry kept");
    CHECK(tblob(1, 0, 0, 0, 0) == 1, "and the note recorded");
    p = (const uint8_t *)h1_loop(&n);
    CHECK((p[8] | p[9] << 8) == 1536, "at the tape's BARS");
}

static void lane_entry_past_bars(void)
{
    static uint8_t copy[4096];
    const uint8_t *p;
    uint32_t n;
    fresh();
    st.seq[1] = 3;                               /* BARS 8 */
    go(20);
    lane(6, 0, 5);
    CHECK(eng_key_at(6) == key_byte(5, 1), "bar 7: F");
    st.seq[1] = 2;                               /* BARS 4 */
    tap(H1_BTN_PLAY);
    go(200);
    tap(H1_BTN_PLAY);
    CHECK(tblob(4, 0, 0, 0, 0) == 1, "BARS 4: the entry past the end is kept");
    p = (const uint8_t *)h1_loop(&n);
    memcpy(copy, p, n);
    fresh();
    st.seq[1] = 2;
    CHECK(h1_loop_load(copy, n) == 0 && tblob(4, 0, 0, 0, 0) == 1, "through a save and a load too");
    st.seq[1] = 3;                               /* BARS 8 again */
    go(20);
    CHECK(eng_key_at(6) == key_byte(5, 1) && eng_lane_get(6, 0) == 5, "BARS 8: it applies again at bar 7");
}

/* ---- the LANE subpage and the deck ---- */
static void to_lane(void)                        /* SEQ, then SELECT twice */
{
    tap(H1_BTN_SEQ);
    h1_encoder(H1_ENC_SELECT, 1); go(40);
    h1_encoder(H1_ENC_SELECT, 1); go(40);
}
static const char *ctl(int enc, int *lo, int *hi, const char **name)
{
    const char *tx = 0;
    h1_control(enc, lo, hi, name, &tx);
    return tx ? tx : "";
}
static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static void lane_subpage(void)
{
    const char *nm;
    int lo, hi;
    fresh();
    to_lane();
    CHECK(ui_subpage() == 2, "SEQ, SELECT x2: subpage 2");
    ctl(H1_ENC_K1, &lo, &hi, &nm);
    CHECK(same(nm, "BAR"), "K1 is BAR");
    ctl(H1_ENC_K2, &lo, &hi, &nm);
    CHECK(same(nm, "ROOT"), "K2 is ROOT");
    ctl(H1_ENC_K3, &lo, &hi, &nm);
    CHECK(same(nm, "SCALE"), "K3 is SCALE");
    ctl(H1_ENC_K4, &lo, &hi, &nm);
    CHECK(same(nm, "DEG"), "K4 is DEG");
    CHECK(same(ctl(H1_ENC_K1, &lo, &hi, &nm), "1"), "BAR 1 to start");
    CHECK(same(ctl(H1_ENC_K2, &lo, &hi, &nm), "-"), "ROOT shows a dash with no entry");
    CHECK(same(ctl(H1_ENC_SELECT, &lo, &hi, &nm), "3 LANE"), "SELECT is spoken as 3 LANE");
}
static void bar_clamps(void)
{
    const char *nm;
    int lo, hi;
    fresh();
    st.seq[1] = 2;                               /* BARS 4 */
    go(40);
    to_lane();
    ctl(H1_ENC_K1, &lo, &hi, &nm);
    CHECK(hi == 3 && same(ctl(H1_ENC_K1, &lo, &hi, &nm), "1"), "BARS 4: BAR's max is bar 4");
    h1_control_set(H1_ENC_K1, 3);
    go(40);
    CHECK(same(ctl(H1_ENC_K1, &lo, &hi, &nm), "4"), "BAR 4");
    h1_control_set(H1_ENC_K1, 9);
    go(40);
    CHECK(same(ctl(H1_ENC_K1, &lo, &hi, &nm), "4"), "BAR does not go past the tape");
    st.seq[1] = 1;                               /* BARS 2 */
    go(40);
    CHECK(same(ctl(H1_ENC_K1, &lo, &hi, &nm), "2") && hi == 1, "BARS 2: the cursor clamps to bar 2");
}
static void knobs_set_entry(void)
{
    const char *nm;
    int lo, hi;
    fresh();
    to_lane();
    h1_control_set(H1_ENC_K1, 2);                /* bar 3 */
    go(40);
    h1_control_set(H1_ENC_K2, 3);                /* C, C#, D */
    go(40);
    CHECK(eng_lane_get(2, 0) == 2, "ROOT D at bar 3");
    CHECK(eng_lane_get(2, 1) == -1 && eng_lane_get(2, 2) == -1, "its other fields are dashes");
    CHECK(same(ctl(H1_ENC_K2, &lo, &hi, &nm), "D"), "the knob shows D at once");
    h1_control_set(H1_ENC_K3, 2);
    h1_control_set(H1_ENC_K4, 4);
    go(40);
    CHECK(eng_lane_get(2, 1) == 1 && eng_lane_get(2, 2) == 3, "SCALE MAJOR, DEG IV");
    CHECK(same(ctl(H1_ENC_K3, &lo, &hi, &nm), "MAJOR") && same(ctl(H1_ENC_K4, &lo, &hi, &nm), "IV"), "shown");
    h1_control_set(H1_ENC_K1, 0);
    go(40);
    CHECK(same(ctl(H1_ENC_K2, &lo, &hi, &nm), "-"), "bar 1 has no entry: dashes");
    h1_control_set(H1_ENC_K1, 2);
    go(40);
    CHECK(same(ctl(H1_ENC_K2, &lo, &hi, &nm), "D"), "back on bar 3: D, re-read");
    h1_control_set(H1_ENC_K2, 1);                /* C */
    go(40);
    h1_control_set(H1_ENC_K2, 0);                /* turned down past C */
    go(40);
    CHECK(eng_lane_get(2, 0) == -1 && same(ctl(H1_ENC_K2, &lo, &hi, &nm), "-"), "past C is a dash");
    CHECK(eng_lane_get(2, 1) == 1, "the entry stays with SCALE and DEG");
    h1_control_set(H1_ENC_K3, 0);
    h1_control_set(H1_ENC_K4, 0);
    go(40);
    CHECK(eng_lane_get(2, 1) == -1 && eng_lane_get(2, 2) == -1, "all three dashes: no entry");
}
static long cell_sum(int b, int bars)            /* the lit pixels in bar b's cell (bars up to 8 across) */
{
    int cw = 224 / bars, x, y;
    long sum = 0;
    for (y = 72; y < 124; y++)
        for (x = 8 + b * cw + 3; x < 8 + (b + 1) * cw - 3; x++)
            sum += g_fb[y * FB_W + x];
    return sum;
}
static void cells_drawn(void)
{
    fresh();
    st.seq[1] = 2;                               /* BARS 4 */
    go(40);
    to_lane();
    h1_control_set(H1_ENC_K1, 3);                /* the cursor on bar 4, out of the way */
    go(40);
    lane(1, 2, 3);                               /* an entry at bar 2: DEG IV */
    go(100);
    CHECK(cell_sum(0, 4) != cell_sum(1, 4), "bar 2's cell (an entry) differs from bar 1's (before any)");
    CHECK(cell_sum(1, 4) != cell_sum(2, 4), "an entry in full colour differs from the dim inherited bar after it");
    h1_control_set(H1_ENC_K1, 0);
    go(100);
    CHECK(cell_sum(0, 4) != cell_sum(2, 4), "the cursor's cell is lit");
}
static long under_sum(int b, int bars)           /* the underline band (rows 97 to 100) of bar b's cell */
{
    int cw = 224 / bars, x, y;
    long sum = 0;
    for (y = 97; y < 100; y++)
        for (x = 8 + b * cw + 3; x < 8 + (b + 1) * cw - 3; x++)
            sum += g_fb[y * FB_W + x];
    return sum;
}
static void minor_underlined(void)
{
    fresh();
    st.seq[1] = 2;
    go(40);
    to_lane();
    h1_control_set(H1_ENC_K1, 3);
    go(40);
    lane(0, 1, 1);                               /* bar 1: C MAJOR */
    lane(0, 2, 1);                               /* DEG II: a minor triad (D F A) */
    lane(1, 2, 3);                               /* bar 2: DEG IV: major (F A C) */
    go(100);
    CHECK(under_sum(0, 4) != under_sum(1, 4), "a minor degree (II in C MAJOR) is underlined, a major one (IV) is not");
    lane(1, 1, 0);                               /* bar 2: CHROM: DEG does nothing, upper case */
    lane(1, 2, 1);
    go(100);
    CHECK(under_sum(1, 4) == under_sum(2, 4), "CHROM: no underline (as the empty cell after it)");
}
static void deck_mark_and_header(void)
{
    fresh();
    st.seq[1] = 2;
    go(40);
    tap(H1_BTN_SEQ);                             /* the deck */
    CHECK(ui_subpage() == 0, "the deck");
    CHECK(same(ui_lane_head(), ""), "no entries: no key in the header");
    lane(2, 0, 2);                               /* bar 3: D */
    lane(2, 1, 1);                               /* MAJOR */
    lane(2, 2, 3);                               /* IV */
    go(60);
    CHECK(g_fb[170 * FB_W + 129] != g_fb[170 * FB_W + 90], "the lane band has a mark at bar 3");
    CHECK(same(ui_lane_head(), ""), "stopped: no key in the header");
    eng_post(EV_PLAY, 0);
    go(100);
    CHECK(same(ui_lane_head(), "- - -") == 0 && ui_lane_head()[0], "playing in bar 1: a header reads");
    go(2500);                                    /* 240 BPM: a bar is a second, so bar 3 */
    CHECK(same(ui_lane_head(), "D MAJ . IV"), "bar 3: the key in force, D MAJ . IV");
    eng_post(EV_PLAY, 0);
    go(100);
}
static void step_mode_has_no_lane(void)
{
    const char *nm;
    int lo, hi;
    fresh();
    tap(H1_BTN_SEQ);
    ctl(H1_ENC_SELECT, &lo, &hi, &nm);
    CHECK(hi == 2, "tape mode: three subpages");
    h1_control_set(H1_ENC_SELECT, 2);
    go(40);
    st.seq[0] = SEQ_STEP;
    eng_post(EV_SEQ_MODE, 0);
    go(40);
    ctl(H1_ENC_SELECT, &lo, &hi, &nm);
    CHECK(hi == 1 && ui_subpage() <= 1, "STEP mode: SEQ keeps its two pages, the open LANE is left");
}

int main(void)
{
    lane_set_and_clear();
    lane_inherits();
    no_wrap();
    follows_bar_by_bar();
    deg_chrom_nothing();
    key_off_ignores_lane();
    boundary();
    record_over_deg();
    live_keys_snap_to_lane();
    step_mode_ignores_lane();
    chord_scale_follows_lane();
    glo_change_rebuilds();
    meter_change_keeps_tick();
    lane_tail_in_blob();
    lane_survives();
    lane_only_tape_overdubs();
    lane_entry_past_bars();
    lane_subpage();
    bar_clamps();
    knobs_set_entry();
    cells_drawn();
    minor_underlined();
    deck_mark_and_header();
    step_mode_has_no_lane();
    printf("lane: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
