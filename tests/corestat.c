/* How the voices of a block are shared between the FM-1's two cores, on the host. Not a test: a measurement,
 * for when the split in block() (core/synth.c) changes. Each scenario sets up some tracks and engines, then
 * renders one 32-sample block at a time and counts: the voices rendered, the voices handed to the second core,
 * the handshakes (c2_run calls), and an estimate of the first core's time per block if the second core ran
 * alongside it. The second core here is not a thread: run only takes the job, wait does it and times it, and
 * the time from run to wait is what the first core did meanwhile, so the first core's share is the block's
 * time less each job plus whatever of a job outlasted the first core's own work. The host is much faster than
 * the FM-1, so read the times against each other, not against the FM-1's 726 us. It also prints a hash of the
 * output, on one core and on two, so a change to the split can be checked against the one before it. The screen's
 * frame, drawn every 32 blocks here, is timed apart and left out: on the FM-1 it runs outside the audio.
 *
 * Fewer handshakes here is not a faster FM-1. Gathering all of a block's voices into one job list, alternated
 * over both cores with one handshake (tried 2026-11-22, not kept), took the tape scenario from 3 handshakes to 1
 * here, and cost the FM-1 1 to 6 points of AUDIO (CC 119): tape + 4 HAZE keys went from 81% to 85%. The
 * per-part handshakes seem to keep both cores on the same engine's code at once. Measure there before keeping
 * a change.
 *
 *   cc -std=c11 -O2 -ffp-contract=off -w $(find core -name '*.c' ! -name synth.c) tests/corestat.c -o build/corestat -lm
 *   build/corestat
 *
 * (It includes core/synth.c itself, to see which voices sounded.) */
#define _POSIX_C_SOURCE 200809L                  /* clock_gettime and popen under -std=c11 on glibc */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../core/synth.c"

static double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec * 1e-3;
}

/* ---- the second core, done at wait ---- */
static void (*job_fn)(uint32_t);
static uint32_t job_arg;
static double t_run, blk_jobs, blk_extra;
static int blk_calls, blk_c2;

static int run2(void (*fn)(uint32_t), uint32_t arg)
{
    job_fn = fn;
    job_arg = arg;
    blk_calls++;
    blk_c2 += __builtin_popcount(arg);
    t_run = now_us();
    return 1;
}
static int wait2(void)
{
    double t0 = now_us(), gap = t0 - t_run, job;
    job_fn(job_arg);
    job = now_us() - t0;
    blk_jobs += job;
    if (job > gap) blk_extra += job - gap;
    return 0;
}

/* ---- the panel ---- */
static uint32_t ms;
static uint64_t hash;
static int32_t out[2 * CTRL];

static double t_frame;                           /* (the screen's frame, drawn in a block: not audio, taken out of its time) */
static void blk1(void)
{
    static int frames;
    int i;
    h1_render(out, CTRL);
    for (i = 0; i < 2 * CTRL; i++) {
        hash ^= (uint32_t)out[i];
        hash *= 1099511628211ull;
    }
    if (++frames % 32 == 0) {
        double t0 = now_us();
        h1_frame(ms += 23);
        t_frame += now_us() - t0;
    }
}
static void go(int msec)
{
    int n = msec * 441 / 320 + 1;
    while (n--) blk1();
}
static void track(int d) { h1_button(H1_BTN_EDIT, 1); h1_encoder(H1_ENC_PRESET, d); h1_button(H1_BTN_EDIT, 0); }
static void engine(int inst, int algo)
{
    h1_control_set(H1_ENC_PRESET, inst);
    go(20);
    h1_control_set(H1_ENC_ALGO, algo);
    go(20);
}
static void rec(void) { h1_button(H1_BTN_REC, 1); h1_button(H1_BTN_REC, 0); }
static void keys(const int *k, int n, int down)
{
    int i;
    for (i = 0; i < n; i++) h1_key(k[i], down);
}
/* a take on the track in hand: n keys held for most of the tape's one bar (2 s), so they sound through its loop */
static void take(const int *k, int n)
{
    rec();
    go(30);
    keys(k, n, 1);
    go(1800);
    keys(k, n, 0);
    go(60);
    rec();                                       /* the take closes and plays on */
    go(30);
}

enum { FM4 = 0, ELECTRIC = 1, STACK = 2, SHAPER = 4, KIT = 8 , HAZE = 6 };
static const int k2a[2] = {7, 11}, k2b[2] = {14, 18}, k4[4] = {2, 9, 16, 21}, k8[8] = {0, 3, 7, 10, 14, 17, 21, 24};

static void sc_haze8(void) { engine(HAZE, 2); keys(k8, 8, 1); }
static void sc_tape(void)                        /* the tester's: T1 FM-4 and T2 STACK on the tape, HAZE on T3 */
{
    engine(FM4, 6); take(k2a, 2);
    track(1); go(30); engine(STACK, 1); take(k2b, 2);
    track(1); go(30); engine(HAZE, 2); keys(k4, 4, 1);
}
static void sc_odd(void)                         /* one note on each of T1 (FM-4) and T2 (STACK), HAZE x5 on T3 */
{
    engine(FM4, 6); take(k2a, 1);
    track(1); go(30); engine(STACK, 1); take(k2b, 1);
    track(1); go(30); engine(HAZE, 2); keys(k8, 5, 1);
}
static void sc_light(void)                       /* one note on the tape on T1 (FM-4), one live HAZE key on T2 */
{
    engine(FM4, 6); take(k2a, 1);
    track(1); go(30); engine(HAZE, 2); keys(k4, 1, 1);
}
static void sc_same(void)                        /* HAZE on the tape on T1, HAZE (another algorithm) live on T2 */
{
    engine(HAZE, 2); take(k2a, 2);
    track(1); go(30); engine(HAZE, 5); keys(k4, 4, 1);
}
static void sc_onetrack(void)                    /* FM-4 on the tape on T1, STACK live on T1 too */
{
    engine(FM4, 6); take(k2a, 2);
    engine(STACK, 1); keys(k4, 4, 1);
}
static void sc_onetrack_same(void)               /* FM-4 on the tape on T1, FM-4 (another algorithm) live on T1 */
{
    engine(FM4, 6); take(k2a, 2);
    engine(FM4, 3); keys(k4, 4, 1);
}

/* a drum take on the track in hand: kick, closed hat and snare (KIT's C, F#, D) as sixteenth steps over the bar */
static void take_beat(void)
{
    static const int pat[16] = {7, 13, 13, 7, 9, 13, 7, 13, 7, 13, 13, 7, 9, 13, 13, 13};
    int i;
    rec();
    go(30);
    for (i = 0; i < 16; i++) { h1_key(pat[i], 1); go(40); h1_key(pat[i], 0); go(72); }
    go(60);
    rec();
    go(30);
}
static void sc_reddit3(void)                     /* a tester's jam (r/MVaveFM1, 0.4.0): SHAPER CZ-SAW, ELECTRIC FELT,
                                                  * KIT WORLD on the tape; "any sound on T4" and it stuttered */
{
    engine(SHAPER, 6); take(k2a, 2);
    track(1); go(30); engine(ELECTRIC, 5); take(k2b, 2);
    track(1); go(30); engine(KIT, 7); take_beat();
}
static void sc_reddit4(void)                     /* the same, and three keys held live on T4 (STACK) */
{
    sc_reddit3();
    track(1); go(30); engine(STACK, 1); keys(k4, 3, 1);
}

typedef struct { const char *name; void (*setup)(void); } Sc;
static const Sc scs[] = {
    {"haze8      8 HAZE keys", sc_haze8},
    {"tape       T1 FM-4 x2, T2 STACK x2 (tape), T3 HAZE x4", sc_tape},
    {"odd        T1 FM-4 x1, T2 STACK x1 (tape), T3 HAZE x5", sc_odd},
    {"light      T1 FM-4 x1 (tape), T2 HAZE x1", sc_light},
    {"same       T1 HAZE x2 (tape), T2 HAZE x4 other algo", sc_same},
    {"onetrack   T1 FM-4 x2 (tape) + STACK x4 live on T1", sc_onetrack},
    {"onetrack=  T1 FM-4 x2 (tape) + FM-4 x4 other algo", sc_onetrack_same},
    {"reddit3    T1 SHAPER CZ-SAW x2, T2 ELECTRIC FELT x2, T3 KIT WORLD beat (tape)", sc_reddit3},
    {"reddit4    the same + T4 STACK x3 live", sc_reddit4},
};
#define NSC (int)(sizeof scs / sizeof scs[0])
#define MEASURE 6000                             /* blocks, about 4.4 s */

static void scenario(const Sc *s, int two, uint64_t *h_out)
{
    long nblk = 0, v_all = 0, v_c2 = 0, calls = 0, parts = 0;
    double t_all = 0.0, t_c1 = 0.0;
    int b, i;
    h1_init(44100);
    h1_two_cores(two ? run2 : 0, two ? wait2 : 0);
    ms = 0;
    hash = 1469598103934665603ull;
    st.fx_on = 0;
    st.seq[1] = 0;                               /* the tape one bar long */
    go(50);
    s->setup();
    go(300);
    for (b = 0; b < MEASURE; b++) {
        f32 age0[NVOICE];
        uint16_t ks[NVOICE];
        int nr = 0, nk = 0, j;
        double t0, t;
        for (i = 0; i < NVOICE; i++) age0[i] = voices[i].age;
        blk_calls = blk_c2 = 0;
        blk_jobs = blk_extra = 0.0;
        t_frame = 0.0;
        t0 = now_us();
        blk1();
        t = now_us() - t0 - t_frame;
        for (i = 0; i < NVOICE; i++) {
            uint16_t k;
            if (voices[i].age == age0[i] && !voices[i].stage) continue;
            nr++;
            k = (uint16_t)(voices[i].trk | voices[i].part << 2 | voices[i].seq << 10);
            for (j = 0; j < nk && ks[j] != k; j++) ;
            if (j == nk) ks[nk++] = k;
        }
        nblk++;
        v_all += nr;
        v_c2 += blk_c2;
        calls += blk_calls;
        parts += nk;
        t_all += t;
        t_c1 += t - blk_jobs + blk_extra;
    }
    *h_out = hash;
    if (!two) {
        printf("  one core: %.2f voices, %.2f parts a block; %.2f us a block\n",
               (double)v_all / nblk, (double)parts / nblk, t_all / nblk);
        return;
    }
    printf("  two cores: core 1 %.2f voices, core 2 %.2f; %.2f handshakes a block; core 1 %.2f us a block\n",
           (double)(v_all - v_c2) / nblk, (double)v_c2 / nblk, (double)calls / nblk, t_c1 / nblk);
}

/* each scenario and core count in a process of its own (the core keeps some state through h1_init):
 * no argument runs them all through popen, "corestat S N" runs scenario S on N cores */
int main(int argc, char **argv)
{
    int s, two;
    if (argc == 3) {
        uint64_t h;
        s = argv[1][0] - '0';
        two = argv[2][0] == '2';
        if (s < 0 || s >= NSC) return 1;
        scenario(&scs[s], two, &h);
        printf("  hash %016llx\n", (unsigned long long)h);
        return 0;
    }
    for (s = 0; s < NSC; s++) {
        char hs[2][64], cmd[1024], line[256];
        printf("%s\n", scs[s].name);
        fflush(stdout);
        for (two = 0; two < 2; two++) {
            FILE *f;
            snprintf(cmd, sizeof cmd, "'%s' %d %d", argv[0], s, two + 1);
            f = popen(cmd, "r");
            hs[two][0] = 0;
            while (f && fgets(line, sizeof line, f)) {
                if (!strncmp(line, "  hash ", 7)) snprintf(hs[two], sizeof hs[two], "%.16s", line + 7);
                else fputs(line, stdout);
            }
            if (f) pclose(f);
        }
        printf("  hash %s %s\n", hs[0], strcmp(hs[0], hs[1]) ? "TWO CORES DIFFER" : "(one core = two)");
    }
    return 0;
}
