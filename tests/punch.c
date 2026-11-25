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

    printf("punch: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
