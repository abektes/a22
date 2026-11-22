/* PERC with eight modes and sympathetic strings (sprint 6). Run by tests/run_tests.sh.
 *
 *   every note     every algorithm, low, middle and high notes, MALLET and RING at both ends: sounds, stays
 *                  inside the limiter, and is silent again a few seconds after it is let go
 *   eight modes    each algorithm's four new modes are heard: a middle C with all eight differs from the same
 *                  with h1's four by more than -25 dB of the whole
 *   sympathetic    SPACE above 60 brings in strings tuned an octave and a twelfth over the note, ringing on
 *                  after the modes have died away; at 60 and below there are none (SPACE's default, 52, sounds
 *                  as it did)
 *
 * Each take in a process of its own (as tests/basics.c). Key 7 is middle C. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "../core/internal.h"
#include "../core/h1.h"

#define PERC 9
#define SR 44100
static uint32_t ms;
static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

static float mid[6 * SR];
static int nmid;
typedef struct Heard { double rms, late, at[4]; int32_t peak; } Heard;

static void go(int msec)
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i, j;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        for (j = 0; j < 512; j += 2) {
            if (nmid < 6 * SR) mid[nmid++] = (float)(((double)out[j] + (double)out[j + 1]) * 0.5 / 8388608.0);
        }
        if (i % 3 == 0) h1_frame(ms += 17);
    }
}
static double energy(int a, int n)
{
    double e = 0.0;
    int i;
    for (i = a; i < a + n && i < nmid; i++) e += (double)mid[i] * mid[i];
    return e;
}
static double peakof(int a, int n)
{
    double m = 0.0;
    int i;
    for (i = a; i < a + n && i < nmid; i++) if (fabs(mid[i]) > m) m = fabs(mid[i]);
    return m;
}
static double goertzel(int a, int n, double hz)
{
    double w = 2.0 * cos(6.283185307179586 * hz / SR), s1 = 0.0, s2 = 0.0;
    int i;
    for (i = a; i < a + n && i < nmid; i++) {
        double s0 = mid[i] + w * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - w * s1 * s2;
}

static Heard taken(Heard (*fn)(int, int), int a, int b)
{
    int fd[2];
    Heard h;
    pid_t pid;
    fflush(stdout);
    if (pipe(fd) != 0 || (pid = fork()) < 0) { printf("FAIL: no process for a take\n"); exit(1); }
    if (pid == 0) {
        h = fn(a, b);
        _exit(write(fd[1], &h, sizeof h) == (ssize_t)sizeof h ? 0 : 1);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != (ssize_t)sizeof h) { printf("FAIL: a take did not come back\n"); exit(1); }
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}

static void perc(int algo)
{
    h1_init(SR);
    st.fx_on = 0;
    h1_control_set(H1_ENC_PRESET, PERC);
    h1_control_set(H1_ENC_ALGO, algo);
    go(200);
    nmid = 0;
}

/* ---- every note: algorithm a, key k, MALLET and RING from corner c ---- */
static Heard note(int a, int kc)
{
    Heard h;
    int k = kc & 0xFF, c = kc >> 8;
    memset(&h, 0, sizeof h);
    perc(a);
    h1_control_set(H1_ENC_K2, c & 1 ? 100 : 0);
    h1_control_set(H1_ENC_K3, c & 2 ? 100 : 0);
    h1_control_set(H1_ENC_K4, 100);              /* (the sympathetic strings in too) */
    go(100);
    nmid = 0;
    h1_key(k, 1);
    go(600);
    h1_key(k, 0);
    go(4400);
    h.rms = sqrt(energy(0, SR / 2) / (SR / 2));
    h.peak = (int32_t)(peakof(0, nmid) * 8388608.0);
    h.late = sqrt(energy(nmid - SR / 2, SR / 2) / (SR / 2));
    return h;
}

/* ---- a middle C on algorithm a, SPACE at sp: energy at four frequencies, early and late ---- */
static const double HZ0 = 261.6256;
static Heard strike(int a, int sp)
{
    Heard h;
    memset(&h, 0, sizeof h);
    perc(a);
    h1_control_set(H1_ENC_K1, 50);
    h1_control_set(H1_ENC_K2, sp < 0 ? 100 : 80);   /* (sp < 0: the modes test, MALLET and RING up) */
    h1_control_set(H1_ENC_K3, sp < 0 ? 100 : 50);
    h1_control_set(H1_ENC_K4, sp < 0 ? 50 : sp);
    go(100);
    nmid = 0;
    h1_key(7, 1);
    go(2500);
    h1_key(7, 0);
    go(200);
    h.rms = sqrt(energy(0, SR / 4) / (SR / 4));
    return h;
}

/* a middle C on algorithm a with n modes, rendered in a process of its own, into out[6 s] */
static void take_modes(int a, int n, float *out)
{
    int fd[2];
    pid_t pid;
    size_t got = 0, want = sizeof(float) * 6 * SR;
    fflush(stdout);
    if (pipe(fd) != 0 || (pid = fork()) < 0) { printf("FAIL: no process for a take\n"); exit(1); }
    if (pid == 0) {
        size_t put = 0;
        close(fd[0]);
        perc_test_modes(n);
        strike(a, 50);
        for (; nmid < 6 * SR; nmid++) mid[nmid] = 0.0f;
        while (put < want) {
            ssize_t w = write(fd[1], (char *)mid + put, want - put);
            if (w <= 0) _exit(1);
            put += (size_t)w;
        }
        _exit(0);
    }
    close(fd[1]);
    while (got < want) {
        ssize_t r = read(fd[0], (char *)out + got, want - got);
        if (r <= 0) { printf("FAIL: a take did not come back\n"); exit(1); }
        got += (size_t)r;
    }
    close(fd[0]);
    waitpid(pid, 0, 0);
}

int main(void)
{
    static const int KEYS[3] = {0, 7, 26};
    char what[200];
    int a, k, c;

    {   /* every note */
        int sounds = 1, inside = 1, quiet = 1;
        for (a = 0; a < 8; a++)
            for (k = 0; k < 3; k++)
                for (c = 0; c < 4; c++) {
                    Heard h = taken(note, a, KEYS[k] | (c << 8));
                    const char *nm = h1_algo_name(PERC, a);
                    if (h.rms < 2e-3) { sounds = 0; printf("  %s key %d corner %d: rms %.4f\n", nm, KEYS[k], c, h.rms); }
                    if (h.peak >= 0x7FFFFF) { inside = 0; printf("  %s key %d corner %d: peak %d\n", nm, KEYS[k], c, (int)h.peak); }
                    if (h.late > 2e-5) { quiet = 0; printf("  %s key %d corner %d: %.2g left\n", nm, KEYS[k], c, h.late); }
                }
        CHECK(sounds, "every algorithm sounds, low to high, MALLET and RING at both ends");
        CHECK(inside, "and stays inside the limiter");
        CHECK(quiet, "and is silent again 4 s after the key is let go");
    }

    {   /* eight modes: a middle C with all eight against the same with h1's four (each take a process of its
         * own, so both start from the same random numbers: two takes with four are the same to the bit) */
        static float four[6 * SR], eight[6 * SR], again[6 * SR];
        for (a = 0; a < 8; a++) {
            double diff = 0.0, all = 0.0, same = 0.0;
            int i;
            take_modes(a, 4, four);
            take_modes(a, 8, eight);
            if (a == 0) {
                take_modes(a, 4, again);
                for (i = 0; i < 6 * SR; i++) same += fabs((double)again[i] - four[i]);
                CHECK(same == 0.0, "two takes with four modes are the same to the bit");
            }
            for (i = 0; i < 6 * SR; i++) {
                double d = (double)eight[i] - four[i];
                diff += d * d;
                all += (double)eight[i] * eight[i];
            }
            snprintf(what, sizeof what, "%s: the four new modes are heard (%.1f dB under the whole)",
                     h1_algo_name(PERC, a), 10.0 * log10(diff / all + 1e-30));
            CHECK(diff > 0.003 * all, what);
        }
    }

    {   /* sympathetic strings, on WOOD (its modes die in a quarter second): late energy at 2 f0 */
        int sp;
        double late[3];
        static const int SP[3] = {52, 60, 100};
        for (sp = 0; sp < 3; sp++) {
            pid_t pid;
            int fd[2];
            double e;
            fflush(stdout);
            if (pipe(fd) != 0 || (pid = fork()) < 0) return 1;
            if (pid == 0) {
                strike(4, SP[sp]);
                e = goertzel(SR, SR, 2.0 * HZ0) + goertzel(SR, SR, 3.0 * HZ0);
                _exit(write(fd[1], &e, sizeof e) == (ssize_t)sizeof e ? 0 : 1);
            }
            close(fd[1]);
            if (read(fd[0], &e, sizeof e) != (ssize_t)sizeof e) return 1;
            close(fd[0]);
            waitpid(pid, 0, 0);
            late[sp] = e;
        }
        snprintf(what, sizeof what, "SPACE 100: strings ring on an octave and a twelfth up (%.3g, at 60 %.3g)", late[2], late[1]);
        CHECK(late[2] > 100.0 * late[1] + 1e-9, what);
        snprintf(what, sizeof what, "SPACE 52 and 60: no strings (%.3g and %.3g)", late[0], late[1]);
        CHECK(late[0] < 1e-6 && late[1] < 1e-6, what);
    }

    printf("perc: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
