/* PUNCH's eight effects on the whole mix, played by the engine's event (EV_PUNCH; the panel's gesture is
 * tested elsewhere). Run by tests/run_tests.sh.
 *
 *   brake       punch 1 on a held note: its zero crossings slow by more than 30%
 *   stutters    punches 2, 3 and 4 at TEMPO 120: the mix's autocorrelation peaks at 1/8, 1/16 and 1/32 (0.25,
 *               0.125, 0.0625 s), and at no shorter lag
 *   lpf         punch 5: open at its start, the mix's brightness down more than 6 dB by the bar's end
 *   hpf         punch 6: open at its start, the mix below 200 Hz down more than 6 dB by the bar's end
 *   crush       punch 7: the mix differs from the same take without it by more than -30 dB
 *   gate        punch 8 at 120: in each 1/16 (125 ms) the second half under 5% of the first's level
 *   no click    at each punch's start and end no sample steps more than 4x the median step before it; CRUSH,
 *               whose held samples step by design, comes in and goes out over its ramp instead
 *   TRICK back  TRICK set to ST 1/8, punch 5 held then let go: no stutter while it's held, the 1/8 stutter after
 *
 * (With no punch the mix is the same to the bit as before: tests/cores.c's and tests/sleep.c's outputs, compared
 * with the commit before PUNCH.)
 *
 * Each take is made in a process of its own, as tests/basics.c's: h1_init does not wind the engine's clocks back,
 * so a take and the same take without the punch only match from the same start. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "../core/internal.h"
#include "../core/h1.h"

#define FR 44100
#define PUNCH_PI 3.14159265358979323846          /* (not M_PI: C99 has none) */
#define MAXS (FR * 4)
static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

/* ---- a take: the left channel, and where its punch started and ended ---- */
typedef struct Take { int32_t n, on, off; int32_t s[MAXS]; } Take;
static Take cap;
static uint32_t ms;

static void rec(int msec)
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i, j;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        for (j = 0; j < 512 && cap.n < MAXS; j += 2)
            cap.s[cap.n++] = out[j];
        if (i % 3 == 0)
            h1_frame(ms += 17);
    }
}
static void punch(int p)
{
    eng_post(EV_PUNCH, p);
    if (p) cap.on = cap.n;
    else cap.off = cap.n;
}

/* the same start for every take: TEMPO, no send effect, FM-4 KEYS made a sine */
static void setup(int bpm)
{
    h1_init(44100);
    st.glo[0] = (uint8_t)bpm;
    st.fx_on = 0;
    h1_control_set(H1_ENC_PRESET, 0);
    h1_control_set(H1_ENC_ALGO, 0);
    h1_control_set(H1_ENC_K2, 0);                /* (DEPTH 0: a sine) */
    memset(&cap, 0, sizeof cap);
    cap.on = cap.off = -1;
}
static void hold(int note) { h1_midi(0x90, (uint8_t)note, 100); }
static void chord(void)                          /* FM-4 ALIEN, every knob up: a chord that never repeats */
{
    h1_control_set(H1_ENC_ALGO, 7);
    h1_control_set(H1_ENC_K1, 100);
    h1_control_set(H1_ENC_K2, 100);
    h1_control_set(H1_ENC_K3, 100);
    h1_control_set(H1_ENC_K4, 100);
    hold(60); hold(66); hold(71);
}

static Take *taken(Take *into, void (*fn)(int), int arg)
{
    int fd[2];
    pid_t pid;
    size_t got = 0;
    fflush(stdout);
    if (pipe(fd) != 0 || (pid = fork()) < 0) {
        printf("FAIL: no process for a take\n");
        exit(1);
    }
    if (pid == 0) {
        const char *p = (const char *)&cap;
        size_t left = sizeof cap;
        close(fd[0]);
        fn(arg);
        while (left) {
            ssize_t w = write(fd[1], p, left);
            if (w <= 0) _exit(1);
            p += w;
            left -= (size_t)w;
        }
        _exit(0);
    }
    close(fd[1]);
    while (got < sizeof *into) {
        ssize_t r = read(fd[0], (char *)into + got, sizeof *into - got);
        if (r <= 0) {
            printf("FAIL: a take did not come back\n");
            exit(1);
        }
        got += (size_t)r;
    }
    close(fd[0]);
    waitpid(pid, 0, 0);
    return into;
}
static Take A, B;

/* ---- measures ---- */
static double energy(const Take *t, int a, int b)
{
    double e = 0.0;
    int i;
    for (i = a; i < b; i++) e += (double)t->s[i] * (double)t->s[i];
    return e;
}
static double slope(const Take *t, int a, int b)    /* the energy of the first difference: how bright */
{
    double e = 0.0;
    int i;
    for (i = a; i < b; i++) {
        double d = (double)t->s[i] - (double)t->s[i - 1];
        e += d * d;
    }
    return e;
}
static double low(const Take *t, int a, int b)      /* the energy under ~200 Hz (a one-pole low-pass) */
{
    double y = 0.0, e = 0.0, k = 1.0 - exp(-2.0 * PUNCH_PI * 200.0 / FR);
    int i;
    for (i = 0; i < b; i++) {
        y += k * ((double)t->s[i] - y);
        if (i >= a) e += y * y;
    }
    return e;
}
static int crossings(const Take *t, int a, int b)
{
    int i, c = 0;
    for (i = a + 1; i < b; i++)
        if ((t->s[i - 1] < 0 && t->s[i] >= 0) || (t->s[i - 1] >= 0 && t->s[i] < 0)) c++;
    return c;
}
static double corr(const Take *t, int a, int n, int lag)   /* normalised, of [a, a+n) with [a+lag, a+lag+n) */
{
    double xy = 0.0, xx = 0.0, yy = 0.0;
    int i;
    for (i = a; i < a + n; i++) {
        double x = (double)t->s[i], y = (double)t->s[i + lag];
        xy += x * y;
        xx += x * x;
        yy += y * y;
    }
    return xx > 0.0 && yy > 0.0 ? xy / sqrt(xx * yy) : 0.0;
}
static double best_corr(const Take *t, int a, int n, int lo, int hi, int step, int *at)
{
    double best = -2.0;
    int k;
    for (k = lo; k <= hi; k += step) {
        double c = corr(t, a, n, k);
        if (c > best) { best = c; if (at) *at = k; }
    }
    return best;
}
static int by_size(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}
static double db(double a, double b) { return 10.0 * log10((a + 1e-9) / (b + 1e-9)); }
static int msx(double msec) { return (int)(msec * FR / 1000.0); }

/* ---- the takes ---- */
static void t_brake(int p) { setup(120); hold(69); rec(500); if (p) punch(p); else cap.on = cap.n; rec(1100); }
static void t_stutter(int p) { setup(120); chord(); rec(600); if (p) punch(p); else cap.on = cap.n; rec(900); }
static void t_sweep(int pn)                       /* pn: the punch << 8 | the note */
{
    setup(120);
    hold(pn & 255);
    rec(400);
    if (pn >> 8) punch(pn >> 8);
    else cap.on = cap.n;
    rec(2300);
}
static void t_crush(int p) { setup(120); hold(69); rec(400); if (p) punch(p); else cap.on = cap.n; rec(700); punch(0); rec(300); }
static void t_gate(int p) { setup(120); hold(69); rec(400); if (p) punch(p); else cap.on = cap.n; rec(1300); }
static void t_click(int hp) { setup(120); hold(69); rec(500); punch(hp & 15); rec(hp >> 4); punch(0); rec(400); }
static void t_trick(int unused)
{
    (void)unused;
    setup(120);
    chord();
    rec(600);
    st.tape[3] = TRICK_STUT8;                    /* (TAPE -> TRICK: the knob, at once) */
    rec(700);
    punch(PUNCH_LPF);
    rec(700);
    punch(0);
    rec(900);
}

/* ---- the hand: FX and a white key (the panel's gesture), the strip, and FX's tap ---- */
static const int WHITE[8] = {0, 2, 4, 6, 7, 9, 11, 12};   /* (SAVE's snapshot keys, punches 1-8) */
static uint8_t mo[256][3];
static int nmo;
static void mo_fn(uint8_t s, uint8_t a, uint8_t b)
{
    if (s < 0xF8 && nmo < 256) { mo[nmo][0] = s; mo[nmo][1] = a; mo[nmo][2] = b; nmo++; }
}
static int ons(void)
{
    int i, n = 0;
    for (i = 0; i < nmo; i++) if ((mo[i][0] & 0xF0) == 0x90 && mo[i][2]) n++;
    return n;
}
static int offs(void)
{
    int i, n = 0;
    for (i = 0; i < nmo; i++) if ((mo[i][0] & 0xF0) == 0x80 || ((mo[i][0] & 0xF0) == 0x90 && !mo[i][2])) n++;
    return n;
}
static void go(int msec)
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        if (i % 3 == 0) h1_frame(ms += 17);
    }
}
static void hand(void)
{
    int k;
    for (k = 0; k < H1_NKEYS; k++) h1_key(k, 0);
    h1_button(H1_BTN_FX, 0);
    h1_init(44100);
    ms = 0;
    nmo = 0;
    st.fx_on = 0;
    h1_midi_out(mo_fn);
    go(100);
}
static void btn(int b, int down) { h1_button(b, down); go(40); }
static void key(int k, int down) { h1_key(k, down); go(40); }
static uint32_t band_sum(void)                   /* the bottom third's pixels, summed with their places */
{
    const uint8_t *fb = h1_framebuffer();
    uint32_t sum = 0, i;
    for (i = 160u * H1_W; i < (uint32_t)H1_W * 236u; i++) sum = sum * 31u + fb[i];
    return sum;
}

static void hand_tests(void)
{
    uint32_t without, with;
    int k, was, hud;

    hand();                                              /* fx_key_starts_and_ends */
    btn(H1_BTN_FX, 1);
    key(WHITE[2], 1);
    CHECK(eng_punch() == 3, "FX + key 3: punch 3 starts");
    key(WHITE[2], 0);
    CHECK(eng_punch() == 0, "its key up ends it, FX still held");
    btn(H1_BTN_FX, 0);

    for (k = 0; k < 8; k++) {                            /* all eight keys */
        char what[64];
        hand();
        btn(H1_BTN_FX, 1);
        key(WHITE[k], 1);
        snprintf(what, sizeof what, "FX + white key %d is punch %d", k + 1, k + 1);
        CHECK(eng_punch() == k + 1, what);
        key(WHITE[k], 0);
        btn(H1_BTN_FX, 0);
    }

    hand();                                              /* key_holds_after_fx_up */
    btn(H1_BTN_FX, 1);
    key(WHITE[2], 1);
    btn(H1_BTN_FX, 0);
    CHECK(eng_punch() == 3, "FX let go first: the key still holds the punch");
    key(WHITE[2], 0);
    CHECK(eng_punch() == 0, "and its key up ends it");

    hand();                                              /* second_key_replaces */
    btn(H1_BTN_FX, 1);
    key(WHITE[2], 1);
    key(WHITE[4], 1);
    CHECK(eng_punch() == 5, "a second key replaces the first");
    key(WHITE[2], 0);
    CHECK(eng_punch() == 5, "letting go of the first changes nothing");
    key(WHITE[4], 0);
    CHECK(eng_punch() == 0, "letting go of the second ends it, the first does not come back");
    btn(H1_BTN_FX, 0);

    hand();                                              /* a punch key plays no note, and no note-off */
    btn(H1_BTN_FX, 1);
    key(WHITE[2], 1);
    key(WHITE[2], 0);
    btn(H1_BTN_FX, 0);
    CHECK(ons() == 0 && offs() == 0, "a punch key sends no note-on and no note-off");

    hand();                                              /* other_keys_play */
    btn(H1_BTN_FX, 1);
    key(1, 1);
    CHECK(ons() == 1, "FX held: a key that is no punch plays a note out over MIDI");
    key(1, 0);
    CHECK(offs() == 1 && eng_punch() == 0, "and its note ends, with no punch");
    btn(H1_BTN_FX, 0);

    hand();                                              /* a key down before FX still ends normally */
    key(WHITE[2], 1);
    btn(H1_BTN_FX, 1);
    key(WHITE[2], 0);
    btn(H1_BTN_FX, 0);
    CHECK(ons() == 1 && offs() == 1 && eng_punch() == 0, "a note held before FX ends with its key");

    hand();                                              /* fx_tap_opens_on_release */
    CHECK(h1_page() == PG_HOME, "starts on HOME");
    btn(H1_BTN_FX, 1);
    CHECK(h1_page() == PG_HOME, "FX down: the page is unchanged");
    btn(H1_BTN_FX, 0);
    CHECK(h1_page() == PG_FX, "FX up: the FX page opens");

    hand();                                              /* fx_used_no_page */
    btn(H1_BTN_FX, 1);
    key(WHITE[0], 1);
    key(WHITE[0], 0);
    btn(H1_BTN_FX, 0);
    CHECK(h1_page() == PG_HOME, "FX used for a punch: no page on release");

    hand();                                              /* bypass is unchanged: EDIT + FX toggles, opens no page */
    was = st.fx_on;
    btn(H1_BTN_EDIT, 1);
    btn(H1_BTN_FX, 1);
    btn(H1_BTN_FX, 0);
    btn(H1_BTN_EDIT, 0);
    CHECK(st.fx_on != was && h1_page() == PG_HOME, "EDIT + FX is bypass and opens no page");

    hand();                                              /* fx_punch_ignores_save_edit */
    was = st.fx_on;
    hud = st.hud;
    btn(H1_BTN_FX, 1);
    key(WHITE[2], 1);
    btn(H1_BTN_SAVE, 1);
    key(WHITE[3], 1);                                    /* (SAVE's snapshot 4 key, were SAVE counted) */
    btn(H1_BTN_SAVE, 0);
    btn(H1_BTN_EDIT, 1);
    btn(H1_BTN_EDIT, 0);
    CHECK(!st.snap[2].used && !st.snap[3].used, "FX held: SAVE makes no snapshot");
    CHECK(st.fx_on == was && st.hud == hud, "FX held: EDIT toggles nothing");
    CHECK(eng_punch() == 4, "and the punches go on");
    btn(H1_BTN_FX, 0);
    key(WHITE[3], 0);
    btn(H1_BTN_EDIT, 1);                                 /* (and EDIT works again once FX is up) */
    btn(H1_BTN_EDIT, 0);
    CHECK(st.hud != hud, "EDIT works again after FX is let go");

    hand();                                              /* SAVE held, then FX: no punch hold, SAVE's gesture works */
    btn(H1_BTN_SAVE, 1);
    btn(H1_BTN_FX, 1);
    go(300);
    CHECK(!h1_fx_held(), "SAVE then FX: FX is not held for punches (so no strip is drawn)");
    key(WHITE[2], 1);
    CHECK(eng_punch() == 0 && h1_punch() == 0, "SAVE then FX then a white key: no punch");
    CHECK(st.snap[2].used, "and the key made SAVE's snapshot as it always did");
    key(WHITE[2], 0);
    btn(H1_BTN_FX, 0);
    btn(H1_BTN_SAVE, 0);

    hand();                                              /* strip_drawn */
    go(300);
    without = band_sum();
    btn(H1_BTN_FX, 1);
    go(300);
    with = band_sum();
    CHECK(with != without, "FX held: the strip changes the bottom third");
    key(WHITE[4], 1);
    go(100);
    CHECK(band_sum() != with, "a held punch is lit on the strip");
    key(WHITE[4], 0);
    btn(H1_BTN_FX, 0);
    go(300);
    CHECK(band_sum() != with, "the strip goes when FX is let go");

    hand();                                              /* fx_light */
    CHECK(!(h1_button_leds() & (1u << H1_BTN_FX)), "FX's light is off to start");
    btn(H1_BTN_FX, 1);
    CHECK(h1_button_leds() & (1u << H1_BTN_FX), "FX held: its light is on");
    key(WHITE[0], 1);                                    /* (a tap would open the FX page, which lights it) */
    key(WHITE[0], 0);
    btn(H1_BTN_FX, 0);
    CHECK(!(h1_button_leds() & (1u << H1_BTN_FX)), "let go: it is off");

    hand();                                              /* the toast */
    btn(H1_BTN_FX, 1);
    key(WHITE[2], 1);
    CHECK(strcmp(h1_toast_now(), "PUNCH ST 1/16") == 0, "a toast names the punch");
    key(WHITE[7], 1);
    CHECK(strcmp(h1_toast_now(), "PUNCH GATE") == 0, "and the next");
    key(WHITE[7], 0);
    key(WHITE[2], 0);
    btn(H1_BTN_FX, 0);
}

/* ---- the tape: punches recorded at tick + 15,360 and played back ---- */
#define LEV_ON 1
#define LEV_PUNCH 3                              /* (the tape's event kinds are core/synth.c's) */
static uint64_t tsmp;
static uint32_t tms;
static double blk_e[16384];                      /* the left channel's energy in each block, from the start */
static signed char blk_p[16384];                 /* eng_punch() after each block */
static uint8_t tmo[256][3];
static int ntmo;
static void tmo_fn(uint8_t s, uint8_t a, uint8_t b)
{
    if (s < 0xF8 && ntmo < 256) { tmo[ntmo][0] = s; tmo[ntmo][1] = a; tmo[ntmo][2] = b; ntmo++; }
}
static double tnow(void) { return (double)tsmp * 1000.0 / 44100.0; }
static void tuntil(double at_ms)
{
    static int32_t out[512];
    while (tnow() < at_ms) {
        size_t bi = (size_t)(tsmp / 256u);
        int j;
        double e = 0.0;
        h1_render(out, 256);
        for (j = 0; j < 512; j += 2) e += (double)out[j] * (double)out[j] * 1e-6;
        if (bi < 16384) { blk_e[bi] = e; blk_p[bi] = (signed char)eng_punch(); }
        tsmp += 256;
        if ((tsmp / 256) % 3 == 0) h1_frame(tms += 17);
    }
}
static int pat(double at_ms) { return blk_p[(size_t)(at_ms * 44.1 / 256.0)]; }
static double eat(double a_ms, double b_ms)      /* the energy between two moments */
{
    size_t i, a = (size_t)(a_ms * 44.1 / 256.0), b = (size_t)(b_ms * 44.1 / 256.0);
    double e = 0.0;
    for (i = a; i < b; i++) e += blk_e[i];
    return e;
}
/* where punch P first and last shows from TZ to TZ + LEN (ms after TZ), -1 if never */
static void span(double tz, double len, int p, double *first, double *last)
{
    double t;
    *first = *last = -1.0;
    for (t = 0.0; t < len; t += 1.0)
        if (pat(tz + t) == p) {
            if (*first < 0.0) *first = t;
            *last = t;
        }
}
static void ttap(int b) { h1_button(b, 1); tuntil(tnow() + 30); h1_button(b, 0); tuntil(tnow() + 30); }
static void tkey(int k, double at, double len) { tuntil(at); h1_key(k, 1); tuntil(at + len); h1_key(k, 0); }
static void tfresh(int bars_code)
{
    int k;
    for (k = 0; k < H1_NKEYS; k++) h1_key(k, 0);
    h1_init(44100);
    h1_two_cores(0, 0);
    ntmo = 0;
    h1_midi_out(tmo_fn);
    st.glo[0] = 120;                             /* (a bar: 2 s) */
    st.seq[1] = (uint8_t)bars_code;
    st.fx_on = 0;
    memset(blk_p, 0, sizeof blk_p);
    tsmp = 0;
    tuntil(200);
}
/* a take on T1: a note to start it, punch P down at ON ms into it and up at OFF (-1: held until after the take) */
static double ttake(int bars_code, int p, double on, double off)
{
    double t0, len = 2000.0 * (bars_code + 1);
    tfresh(bars_code);
    ttap(H1_BTN_REC);
    t0 = tnow() + 50;
    tkey(4, t0, 100);
    tuntil(t0 + on);
    eng_post(EV_PUNCH, (uint32_t)p);
    if (off >= 0.0) {
        tuntil(t0 + off);
        eng_post(EV_PUNCH, 0);
    }
    tuntil(t0 + len + 100);
    if (off < 0.0) eng_post(EV_PUNCH, 0);
    tuntil(t0 + len + 150);
    return t0;
}
static double trestart(void)                     /* the tape from its start; the moment it started */
{
    SeqInfo si;
    eng_seq_info(&si);
    if (si.loop_state != LOOP_IDLE)              /* (playing: stopped first) */
        eng_post(EV_PLAY, 0);
    eng_post(EV_PLAY, 0);
    return tnow();
}
static int tblob(int kind, int *tick, int *d1, int *d2)   /* the saved tape's events of a kind: how many */
{
    uint32_t size = 0, i, n = 0;
    const uint8_t *b = (const uint8_t *)h1_loop(&size);
    for (i = 0; b && 12u + 8u * (i + 1u) <= size; i++) {
        const uint8_t *e = b + 12u + 8u * i;
        if ((e[3] & 7) != kind) continue;
        if (tick && n < 8) tick[n] = e[0] | e[1] << 8;
        if (d1 && n < 8) d1[n] = e[2];
        if (d2 && n < 8) d2[n] = e[6];
        n++;
    }
    return (int)n;
}
static void put_ev(uint8_t *e, int tick, int d1, int flags, int d2)
{
    memset(e, 0, 8);
    e[0] = (uint8_t)tick; e[1] = (uint8_t)(tick >> 8); e[2] = (uint8_t)d1; e[3] = (uint8_t)flags; e[6] = (uint8_t)d2;
}
static uint8_t crafted[12 + 8 * 4];
static uint32_t craft(int n)                     /* a tape blob of N events already in crafted[12..] */
{
    memset(crafted, 0, 12);
    crafted[0] = 0x41; crafted[1] = 0x32; crafted[2] = 0x32; crafted[3] = 0x54;   /* "A22T" */
    crafted[4] = 1; crafted[5] = 1;
    crafted[6] = (uint8_t)n;
    crafted[8] = 0x80; crafted[9] = 0x01;                                         /* 384 */
    return 12u + 8u * (uint32_t)n;
}

static void tape_tests(void)
{
    int tick[8], d1[8], d2[8], n, i;
    double t0, tz, a, b;
    static uint8_t copy[4096];
    uint32_t size;

    /* punch_recorded */
    t0 = ttake(0, 3, 600.0, 900.0);
    n = tblob(LEV_PUNCH, tick, d1, d2);
    CHECK(n == 2, "recorded: a start and an end on the tape");
    if (n == 2) {
        CHECK(d1[0] == 2 && d2[0] == 1 && d2[1] == 0 && d1[1] == 2, "recorded: punch 3 is d1 2; d2 1 starts and 0 ends");
        CHECK(tick[0] >= 15360 + 115 - 6 && tick[0] <= 15360 + 115 + 6, "recorded: the start at tick 15,360 + where it was played");
        CHECK(tick[1] >= 15360 + 173 - 6 && tick[1] <= 15360 + 173 + 6, "recorded: the end at 15,360 + where it was let go");
    }
    (void)t0;

    /* punch_plays_back */
    tz = trestart();
    tuntil(tz + 2100);
    span(tz, 2000.0, 3, &a, &b);
    printf("plays back: punch 3 from %.0f to %.0f ms (recorded 600 to 900)\n", a, b);
    CHECK(a >= 600.0 - 12 && a <= 600.0 + 12 && b >= 900.0 - 12 && b <= 900.0 + 12, "plays back: punch 3 over the same span");
    span(tz, 2000.0, 1, &a, &b);
    CHECK(a < 0.0, "plays back: and no other punch");

    /* punch_tail_in_blob: 0.6.0's loader drops events at tick >= 15,360, so only these may sit there */
    {
        uint32_t sz, k;
        const uint8_t *bl = (const uint8_t *)h1_loop(&sz);
        int ok = 1;
        for (k = 0; bl && 12u + 8u * (k + 1u) <= sz; k++) {
            const uint8_t *e = bl + 12u + 8u * k;
            int tk = e[0] | e[1] << 8, kind = e[3] & 7;
            if ((kind == LEV_PUNCH) != (tk >= 15360)) ok = 0;
        }
        CHECK(ok, "tail: the saved blob has a punch at tick >= 15,360 and nothing else there");
    }
    size = 0;
    {
        const uint8_t *bl = (const uint8_t *)h1_loop(&size);
        memcpy(copy, bl, size);
    }
    tfresh(0);
    CHECK(h1_loop_load(copy, size) == 0, "tail: the blob loads");
    n = tblob(LEV_PUNCH, tick, d1, d2);
    CHECK(n == 2 && tick[0] >= 15360 && tick[1] >= 15360, "tail: and its punches come back at their ticks");
    tz = trestart();
    tuntil(tz + 1200);
    span(tz, 1200.0, 3, &a, &b);
    CHECK(a >= 0.0, "tail: and play");
    tfresh(0);                                   /* a punch below the tail, a note up in it: both dropped */
    put_ev(crafted + 12, 100, 2, LEV_PUNCH, 1);
    put_ev(crafted + 20, 15400, 60, LEV_ON, 100);
    put_ev(crafted + 28, 15460, 2, LEV_PUNCH, 1);
    put_ev(crafted + 36, 40000, 2, LEV_PUNCH, 0);
    CHECK(h1_loop_load(crafted, craft(4)) == 0, "tail: a blob with strays loads");
    CHECK(tblob(LEV_PUNCH, tick, 0, 0) == 1 && tick[0] == 15460 && tblob(LEV_ON, 0, 0, 0) == 0,
          "tail: a punch under the tail, a note in it, a tick past it: dropped; the one in the tail kept");

    /* muted_punch_silent */
    ttake(0, 3, 600.0, 900.0);
    st.trk[0].mute = 1;
    tz = trestart();
    tuntil(tz + 2100);
    span(tz, 2000.0, 3, &a, &b);
    CHECK(a < 0.0, "muted: the track's punch does not play");
    st.trk[0].mute = 0;
    tz = trestart();
    tuntil(tz + 2100);
    span(tz, 2000.0, 3, &a, &b);
    CHECK(a >= 0.0, "muted: unmuted it does");

    /* clear_track_removes_punch */
    ttake(0, 3, 600.0, 900.0);
    eng_post(EV_CLEAR, 2);
    tuntil(tnow() + 40);
    CHECK(tblob(LEV_PUNCH, 0, 0, 0) == 2, "clear: another track's clear leaves the punches");
    eng_post(EV_CLEAR, 1);
    tuntil(tnow() + 40);
    CHECK(tblob(LEV_PUNCH, 0, 0, 0) == 0, "clear: the track's clear takes them");

    /* cut_moves_punch, paste_brings_it, tail events past the tape's length stay */
    tfresh(1);
    ttap(H1_BTN_REC);
    t0 = tnow() + 50;
    tkey(4, t0, 100);
    tuntil(t0 + 600);
    eng_post(EV_PUNCH, 3);
    tuntil(t0 + 900);
    eng_post(EV_PUNCH, 0);
    tkey(7, t0 + 2500, 100);                     /* (a note in bar 2: it stays when bar 1 is cut) */
    tuntil(t0 + 4000 + 100);
    CHECK(tblob(LEV_PUNCH, 0, 0, 0) == 2, "cut: the punch is on a two-bar tape");
    st.tape[2] = 1;                              /* OUT at bar 1 */
    eng_post(EV_CUT, 0);
    tuntil(tnow() + 40);
    CHECK(tblob(LEV_PUNCH, 0, 0, 0) == 0 && tblob(LEV_ON, 0, 0, 0) == 1, "cut: bar 1's punch goes with its notes; bar 2's note stays");
    st.tape[2] = 0;
    st.tape[1] = 1;                              /* IN at bar 2: the tape plays there */
    tuntil(tnow() + 300);
    eng_post(EV_PASTE, 0);
    tuntil(tnow() + 40);
    st.tape[1] = 0;
    n = tblob(LEV_PUNCH, tick, d1, d2);
    CHECK(n == 2, "paste: the punch is back");
    if (n == 2)
        CHECK(tick[0] >= 15360 + 384 + 115 - 6 && tick[0] <= 15360 + 384 + 115 + 6 && d2[0] == 1 && d2[1] == 0,
              "paste: a bar later (where it was pasted), start then end");
    st.seq[1] = 0;                               /* BARS to 1: the punch is in bar 2, past the tape */
    tz = trestart();
    tuntil(tz + 2200);
    span(tz, 2100.0, 3, &a, &b);
    CHECK(a < 0.0 && tblob(LEV_PUNCH, 0, 0, 0) == 2, "length: a punch past the tape's length does not play, and stays stored");

    /* rev_swaps */
    ttake(0, 3, 600.0, 900.0);
    st.tape[0] = TAPE_SPD_REV;
    tz = trestart();
    tuntil(tz + 2100);
    span(tz, 2000.0, 3, &a, &b);
    printf("rev: punch 3 from %.0f to %.0f ms (mirrored: 1100 to 1400)\n", a, b);
    CHECK(a >= 1100.0 - 12 && a <= 1100.0 + 12 && b >= 1400.0 - 12 && b <= 1400.0 + 12, "rev: the punch spans the mirrored span");

    /* tape_punch_across_wrap: held to the loop's end, it ends at the wrap and starts again next pass */
    ttake(0, 3, 1700.0, -1.0);
    CHECK(tblob(LEV_PUNCH, tick, d1, d2) == 2 && d2[0] == 1 && d2[1] == 0 && tick[1] >= 15360 + 380,
          "wrap: a punch held to the end gets its end at the last tick");
    tz = trestart();
    tuntil(tz + 4100);
    CHECK(pat(tz + 1850) == 3, "wrap: punch 3 plays to the end");
    CHECK(pat(tz + 2100) == 0 && pat(tz + 3000) == 0, "wrap: ended at the wrap");
    CHECK(pat(tz + 2000 + 1850) == 3, "wrap: and starts again at its tick next pass");

    /* a punch held when the take ends gets its end there: REC again, then PLAY */
    {
        int by_play;
        for (by_play = 0; by_play < 2; by_play++) {
            double t0;
            char what[96];
            tfresh(0);
            ttap(H1_BTN_REC);
            t0 = tnow() + 50;
            tkey(4, t0, 100);
            tuntil(t0 + 600);
            eng_post(EV_PUNCH, 8);
            tuntil(t0 + 1200);
            ttap(by_play ? H1_BTN_PLAY : H1_BTN_REC);   /* (the take ends with the punch still held) */
            eng_post(EV_PUNCH, 0);
            tuntil(tnow() + 50);
            snprintf(what, sizeof what, "ends held (%s): its start and its end are stored", by_play ? "PLAY" : "REC");
            CHECK(tblob(LEV_PUNCH, tick, d1, d2) == 2 && d2[0] == 1 && d2[1] == 0, what);
            tz = trestart();
            tuntil(tz + 2100);
            span(tz, 2000.0, 8, &a, &b);
            printf("ends held (%s): punch 8 from %.0f to %.0f ms\n", by_play ? "PLAY" : "REC", a, b);
            snprintf(what, sizeof what, "ends held (%s): the punch stops where the take ended, not at the loop's end",
                     by_play ? "PLAY" : "REC");
            CHECK(a > 0.0 && b < 1500.0, what);
        }
    }

    /* hand_wins */
    ttake(0, 3, 400.0, 1400.0);
    tz = trestart();
    tuntil(tz + 500);
    CHECK(pat(tz + 500) == 3, "hand wins: the tape's punch 3 plays");
    tuntil(tz + 700);
    eng_post(EV_PUNCH, 6);
    tuntil(tz + 800);
    CHECK(eng_punch() == 6, "hand wins: punch 6 held by hand: 6");
    eng_post(EV_PUNCH, 0);
    tuntil(tz + 900);
    CHECK(eng_punch() == 3, "hand wins: let go: the tape's 3 is back, its span goes on");
    tuntil(tz + 1600);
    CHECK(eng_punch() == 0, "hand wins: and ends at its end");
    tz = trestart();                             /* the hand holds through the tape's start: it is ignored */
    tuntil(tz + 300);
    eng_post(EV_PUNCH, 6);
    tuntil(tz + 1000);
    CHECK(eng_punch() == 6, "hand wins: held through the tape's start: 6");
    eng_post(EV_PUNCH, 0);
    tuntil(tz + 1100);
    CHECK(eng_punch() == 0, "hand wins: the tape's start was ignored: nothing after");
    tz = trestart();                             /* the tape's end comes while the hand holds: nothing sticks on */
    tuntil(tz + 500);
    tuntil(tz + 700);
    eng_post(EV_PUNCH, 6);
    tuntil(tz + 1600);
    CHECK(eng_punch() == 6, "hand wins: held through the tape's end: 6");
    eng_post(EV_PUNCH, 0);
    tuntil(tz + 1700);
    CHECK(eng_punch() == 0, "hand wins: the tape's end cleared it: nothing sticks on");

    /* stop_ends_punch, HUSH, a track's clear; the hand's punch is the hand's */
    ttake(0, 3, 400.0, 1400.0);
    tz = trestart();
    tuntil(tz + 700);
    CHECK(eng_punch() == 3, "stop: the tape's punch plays");
    eng_post(EV_PLAY, 0);
    tuntil(tnow() + 20);
    CHECK(eng_punch() == 0, "stop: PLAY (stop) mid-punch: 0");
    tz = trestart();
    tuntil(tz + 700);
    eng_post(EV_PANIC, 0);
    tuntil(tnow() + 20);
    CHECK(eng_punch() == 0, "stop: HUSH mid-punch: 0");
    tz = trestart();
    tuntil(tz + 700);
    eng_post(EV_CLEAR, 1);
    tuntil(tnow() + 20);
    CHECK(eng_punch() == 0, "stop: the track's clear mid-punch: 0");
    ttake(0, 3, 400.0, 1400.0);
    tz = trestart();
    tuntil(tz + 700);
    eng_post(EV_PUNCH, 6);
    tuntil(tnow() + 20);
    eng_post(EV_PLAY, 0);
    tuntil(tnow() + 20);
    CHECK(eng_punch() == 6, "stop: the hand's punch is untouched by the stop");
    eng_post(EV_PUNCH, 0);
    tuntil(tnow() + 20);
    CHECK(eng_punch() == 0, "stop: and ends with its key");


    /* an end ends its own punch only: two tracks' punches overlap, 3 on T1 100-300 and 5 on T2 200-400 */
    tfresh(0);
    ttap(H1_BTN_REC);
    t0 = tnow() + 50;
    tkey(4, t0, 100);
    tuntil(t0 + 100);
    eng_post(EV_PUNCH, 3);
    tuntil(t0 + 300);
    eng_post(EV_PUNCH, 0);
    tuntil(t0 + 2100);
    h1_button(H1_BTN_EDIT, 1); h1_encoder(H1_ENC_PRESET, 1); h1_button(H1_BTN_EDIT, 0);   /* to T2 */
    tuntil(t0 + 3800);
    ttap(H1_BTN_REC);                            /* dub on T2 */
    tuntil(t0 + 4200);
    eng_post(EV_PUNCH, 5);
    tuntil(t0 + 4400);
    eng_post(EV_PUNCH, 0);
    tuntil(t0 + 4600);
    ttap(H1_BTN_REC);
    tz = trestart();
    tuntil(tz + 2000);
    CHECK(pat(tz + 150) == 3 && pat(tz + 250) == 5, "overlap: 5 replaces 3 where T2 starts it");
    CHECK(pat(tz + 350) == 5, "overlap: T1's end of 3 does not cut T2's 5");
    CHECK(pat(tz + 450) == 0, "overlap: 5 ends at its own end");

    /* a punch held across a DUB wrap: its end at the last tick, its start at the next pass's first */
    tfresh(0);
    ttap(H1_BTN_REC);
    t0 = tnow() + 50;
    tkey(4, t0, 100);
    tuntil(t0 + 2100);
    ttap(H1_BTN_REC);                            /* dub */
    tuntil(t0 + 3900);
    eng_post(EV_PUNCH, 4);
    tuntil(t0 + 4200);
    eng_post(EV_PUNCH, 0);
    tuntil(t0 + 4400);
    ttap(H1_BTN_REC);
    n = tblob(LEV_PUNCH, tick, d1, d2);
    CHECK(n == 4 && d2[0] == 1 && d2[1] == 0 && d2[2] == 1 && d2[3] == 0 && tick[0] <= 15360 + 6,
          "dub wrap: start at the first tick, end, start, end at the last: no end before its start");
    tz = trestart();
    tuntil(tz + 2000);
    CHECK(pat(tz + 100) == 4 && pat(tz + 300) == 0 && pat(tz + 1000) == 0 && pat(tz + 1950) == 4,
          "dub wrap: it plays as it was held, round the wrap");

    /* mute or solo while its punch plays ends it */
    ttake(0, 3, 400.0, 1400.0);
    tz = trestart();
    tuntil(tz + 700);
    CHECK(eng_punch() == 3, "mute: the tape's punch plays");
    st.trk[0].mute = 1;
    tuntil(tnow() + 8);
    CHECK(eng_punch() == 0, "mute: muted mid-span: 0 within a block");
    st.trk[0].mute = 0;
    tz = trestart();
    tuntil(tz + 700);
    CHECK(eng_punch() == 3, "solo: the tape's punch plays again");
    st.trk[1].mute = 2;
    tuntil(tnow() + 8);
    CHECK(eng_punch() == 0, "solo: another track soloed mid-span: 0");
    st.trk[1].mute = 0;

    /* no_midi */
    ttake(0, 3, 600.0, 900.0);
    tz = trestart();
    tuntil(tz + 2100);
    {
        int bad = 0;
        for (i = 0; i < ntmo; i++)
            if (((tmo[i][0] & 0xF0) != 0x90 && (tmo[i][0] & 0xF0) != 0x80) || tmo[i][1] < 24) bad++;
        CHECK(ntmo > 0 && bad == 0, "no midi: only the notes went out, nothing for a punch");
    }

    /* held_before_rec: a hand punch held when REC starts records nothing until its next start */
    tfresh(0);
    eng_post(EV_PUNCH, 3);
    tuntil(tnow() + 30);
    ttap(H1_BTN_REC);
    t0 = tnow() + 50;
    tkey(4, t0, 100);
    tuntil(t0 + 300);
    eng_post(EV_PUNCH, 0);
    tuntil(t0 + 500);
    CHECK(tblob(LEV_PUNCH, 0, 0, 0) == 0, "held before REC: its end records nothing");
    eng_post(EV_PUNCH, 5);
    tuntil(t0 + 700);
    eng_post(EV_PUNCH, 0);
    tuntil(t0 + 800);
    n = tblob(LEV_PUNCH, tick, d1, d2);
    CHECK(n == 2 && d1[0] == 4 && d1[1] == 4 && d2[0] == 1 && d2[1] == 0, "held before REC: its next start does");

    /* a second key replaces the first: the first ends where the second starts */
    tfresh(0);
    ttap(H1_BTN_REC);
    t0 = tnow() + 50;
    tkey(4, t0, 100);
    tuntil(t0 + 400);
    eng_post(EV_PUNCH, 2);
    tuntil(t0 + 700);
    eng_post(EV_PUNCH, 6);
    tuntil(t0 + 900);
    eng_post(EV_PUNCH, 0);
    tuntil(t0 + 1000);
    n = tblob(LEV_PUNCH, tick, d1, d2);
    CHECK(n == 4 && d1[0] == 1 && d2[0] == 1 && d1[1] == 1 && d2[1] == 0 && d1[2] == 5 && d2[2] == 1 && d2[3] == 0 &&
          tick[1] == tick[2], "replaced: the first's end and the second's start at one tick, then the second's end");

    /* restart_at_the_wrap: a sweep that ends at the wrap and starts again at once starts from its beginning */
    tfresh(0);
    put_ev(crafted + 12, 15360, 5, LEV_PUNCH, 1);                /* HPF at the tape's start, never ended */
    CHECK(h1_loop_load(crafted, craft(1)) == 0, "restart: the crafted tape loads");
    h1_midi(0x90, 69, 100);
    tuntil(tnow() + 400);
    tz = trestart();
    tuntil(tz + 4100);
    {
        int steady = 1;
        double t;
        for (t = 50.0; t < 4000.0; t += 3.0)
            if (pat(tz + t) != 6) steady = 0;
        CHECK(steady, "restart: punch 6 is on all the way through the wrap");
    }
    {
        double late = eat(tz + 1700, tz + 1950), early = eat(tz + 2000 + 100, tz + 2000 + 350);
        printf("restart: the tone %.1f dB down at the end of a pass, %.1f dB over that after the wrap\n",
               db(late, eat(tz + 100, tz + 350)), db(early, late));
        CHECK(db(early, late) > 10.0, "restart: the sweep starts from open again on the next pass (no re-anchor at the wrap)");
    }
}


int main(void)
{
    static const double SECS[3] = {0.25, 0.125, 0.0625};
    int p, i;

    /* brake: a 440 Hz sine's crossings, the punch's second against the half second before */
    taken(&A, t_brake, 1);
    {
        double before = crossings(&A, A.on - FR / 2, A.on) * 2.0, during = crossings(&A, A.on, A.on + FR);
        printf("brake: %.0f crossings a second before, %.0f during\n", before, during);
        CHECK(before > 800.0, "brake: the note sounds before the punch");
        CHECK(during < 0.7 * before, "brake: punch 1 slows the zero crossings by more than 30%");
    }

    /* stutters: the punched mix peaks at its length and at no shorter lag. 1/16 and 1/32 at 120 are 5512.5 and
     * 2756.25 samples, their loops a sample apart in turn, so their peak is lower; all three repeat to the sample
     * every 0.25 s (11025) */
    taken(&B, t_stutter, 0);
    for (p = 2; p <= 4; p++) {
        int L = (int)(SECS[p - 2] * FR), at = 0, a, n = msx(300);
        double rl, rs, rd, r8;
        char what[96];
        taken(&A, t_stutter, p);
        a = A.on + msx(60);
        rl = best_corr(&A, a, n, L - 3, L + 3, 1, &at);
        rs = best_corr(&A, a, n, msx(30), L - msx(5), 2, 0);
        rd = best_corr(&B, a, n, L - 3, L + 3, 1, 0);
        r8 = corr(&A, a, n, FR / 4);
        printf("stutter %d: r %.3f at %d (%d), shorter lags at most %.3f, without it %.3f, at 0.25 s %.3f\n",
               p, rl, at, L, rs, rd, r8);
        snprintf(what, sizeof what, "stutter %d: the mix peaks every %.4f s", p, SECS[p - 2]);
        CHECK(rl > 0.7 && rl > rs + 0.4, what);
        snprintf(what, sizeof what, "stutter %d: and not sooner", p);
        CHECK(rs < 0.5, what);
        snprintf(what, sizeof what, "stutter %d: the same to the sample every 0.25 s", p);
        CHECK(r8 > 0.95, what);
        CHECK(rd < 0.5, "stutters: the mix without a punch does not repeat");
    }

    /* lpf: a 1047 Hz sine, brightness against the take without it, at the start and at the bar's end (2 s) */
    taken(&A, t_sweep, PUNCH_LPF << 8 | 84);
    taken(&B, t_sweep, 84);
    {
        int e = A.on + msx(2000);
        double start = db(slope(&A, A.on + msx(15), A.on + msx(60)), slope(&B, A.on + msx(15), A.on + msx(60)));
        double end = db(slope(&A, e - msx(200), e), slope(&B, e - msx(200), e));
        double held = db(slope(&A, e + msx(50), e + msx(250)), slope(&B, e + msx(50), e + msx(250)));
        printf("lpf: brightness %+.1f dB at the start, %+.1f dB at the bar's end, %+.1f dB after\n", start, end, held);
        CHECK(start > -1.5, "lpf: open at the punch's start");
        CHECK(end < -6.0, "lpf: the brightness down more than 6 dB by the bar's end");
        CHECK(held < -6.0, "lpf: and holding there");
    }

    /* hpf: a 65 Hz sine, the energy under 200 Hz against the take without it */
    taken(&A, t_sweep, PUNCH_HPF << 8 | 36);
    taken(&B, t_sweep, 36);
    {
        int e = A.on + msx(2000);
        double start = db(low(&A, A.on + msx(15), A.on + msx(60)), low(&B, A.on + msx(15), A.on + msx(60)));
        double end = db(low(&A, e - msx(200), e), low(&B, e - msx(200), e));
        printf("hpf: under 200 Hz %+.1f dB at the start, %+.1f dB at the bar's end\n", start, end);
        CHECK(energy(&B, e - msx(200), e) > 0.0, "hpf: the low note sounds");
        CHECK(start > -1.5, "hpf: open at the punch's start");
        CHECK(end < -6.0, "hpf: the energy under 200 Hz down more than 6 dB by the bar's end");
    }

    /* crush: the difference from the take without it */
    taken(&A, t_crush, PUNCH_CRUSH);
    taken(&B, t_crush, 0);
    {
        double err = 0.0, steady = 0.0, in = 0.0, out = 0.0, going = 0.0;
        for (i = A.on + msx(50); i < A.on + msx(650); i++) {
            double d = (double)A.s[i] - (double)B.s[i];
            err += d * d;
            if (fabs(d) > steady) steady = fabs(d);
        }
        for (i = A.on - msx(1); i < A.on + msx(1); i++)        /* (its ramp: 10 ms, so a tenth at 1 ms) */
            if (fabs((double)A.s[i] - (double)B.s[i]) > in) in = fabs((double)A.s[i] - (double)B.s[i]);
        for (i = A.off + msx(1); i < A.off + msx(5); i++)       /* (still going out 1 to 5 ms after) */
            if (fabs((double)A.s[i] - (double)B.s[i]) > going) going = fabs((double)A.s[i] - (double)B.s[i]);
        for (i = A.off + msx(9); i < A.off + msx(30); i++)
            if (fabs((double)A.s[i] - (double)B.s[i]) > out) out = fabs((double)A.s[i] - (double)B.s[i]);
        printf("crush: the difference %+.1f dB of the mix; ramps: %.2f in, %.2f going out, %.2f out of the most\n",
               db(err, energy(&B, A.on + msx(50), A.on + msx(650))), in / (steady + 1e-9), going / (steady + 1e-9),
               out / (steady + 1e-9));
        CHECK(db(err, energy(&B, A.on + msx(50), A.on + msx(650))) > -30.0, "crush: the mix differs by more than -30 dB");
        CHECK(in < 0.25 * steady, "crush: comes in over its ramp");
        CHECK(going > 0.25 * steady && out < 0.25 * steady, "crush: goes out over its ramp");
    }

    /* gate: the 1/16s found from the take (5512.5 samples at 120), each second half against its first */
    taken(&A, t_gate, PUNCH_GATE);
    {
        double six = FR * 0.125, worst = 0.0, best_r = 1e9;
        int off, best = 0, k, nk = 0;
        for (off = 0; off < (int)six; off += 16) {       /* (where the grid sits: the most contrast) */
            double r = 0.0;
            for (k = 1; k < 8; k++) {
                int a = A.on + off + (int)(k * six), h = (int)(six / 2);
                r += energy(&A, a + h, a + 2 * h) / (energy(&A, a, a + h) + 1e-9);
            }
            if (r < best_r) { best_r = r; best = off; }
        }
        for (k = 1; k < 9; k++, nk++) {
            int a = A.on + best + (int)(k * six), h = (int)(six / 2);
            double r = sqrt(energy(&A, a + h, a + 2 * h) / (energy(&A, a, a + h) + 1e-9));
            if (r > worst) worst = r;
        }
        printf("gate: the grid %d samples after the punch (%.0f a 1/16), the second halves at most %.1f%% of the first\n",
               best, six, worst * 100.0);
        CHECK(energy(&A, A.on + msx(200), A.on + msx(1000)) > 0.0, "gate: the note sounds");
        CHECK(worst < 0.05, "gate: each 1/16's second half under 5% of its first");
        CHECK(((A.on + best) % (int)six < msx(3)) || ((A.on + best) % (int)six > (int)six - msx(3)),
              "gate: in time with the beat");
    }

    /* no click: the largest step at each punch's start and end against the median step of the take before it,
     * held 300 ms (BRAKE still sounding, the sweeps near open) and 1.2 s (BRAKE silent, the sweeps far in) */
    for (p = 1; p <= 8; p++) {
        static double steps[MAXS];
        static const int HOLD[2] = {300, 1200};
        int h;
        if (p == PUNCH_CRUSH) continue;
        for (h = 0; h < 2; h++) {
            double med, big = 0.0;
            int ns = 0;
            char what[96];
            taken(&A, t_click, HOLD[h] << 4 | p);
            for (i = msx(100); i < A.on; i++) steps[ns++] = fabs((double)A.s[i] - (double)A.s[i - 1]);
            qsort(steps, (size_t)ns, sizeof steps[0], by_size);
            med = steps[ns / 2];
            for (i = A.on - msx(2); i < A.on + msx(25); i++) big = fmax(big, fabs((double)A.s[i] - (double)A.s[i - 1]));
            for (i = A.off - msx(2); i < A.off + msx(25); i++) big = fmax(big, fabs((double)A.s[i] - (double)A.s[i - 1]));
            printf("no click %d, held %d ms: the largest step %.1fx the median\n", p, HOLD[h], big / (med + 1e-9));
            snprintf(what, sizeof what, "no click: punch %d held %d ms starts and ends without a step over 4x the median",
                     p, HOLD[h]);
            CHECK(med > 0.0 && big < 4.0 * med, what);
        }
    }

    /* TRICK back: the 1/8 stutter gone while punch 5 is held, back once it's let go */
    taken(&A, t_trick, 0);
    {
        int L = (int)(0.25 * FR), n = msx(300);
        double before = best_corr(&A, A.on - msx(600), n, L - 3, L + 3, 1, 0);
        double during = best_corr(&A, A.on + msx(60), n, L - 3, L + 3, 1, 0);
        double after = best_corr(&A, A.off + msx(60), n, L - 3, L + 3, 1, 0);
        printf("TRICK back: r at 1/8 %.3f with TRICK, %.3f with punch 5, %.3f after\n", before, during, after);
        CHECK(before > 0.95, "TRICK back: TRICK's 1/8 stutter plays");
        CHECK(during < 0.9, "TRICK back: punch 5 replaces it while held");
        CHECK(after > 0.95, "TRICK back: the 1/8 stutter is back once it's let go");
    }

    hand_tests();
    tape_tests();

    printf("punch: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
