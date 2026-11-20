/* BARD's drone once the playing stops: it holds through a short gap and then dies away slowly, never cut
 * off. Run by tests/run_tests.sh: the core alone. Each take plays a note on BARD and listens for eight
 * seconds after it; the drone is what a take with DRONE up has that the same take with DRONE down does not
 * (both start from the same engine, so everything else is the same to the sample). */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "../core/internal.h"
#include "../core/h1.h"

#define BARD 12
#define BLK 256
#define WIN 10                                   /* blocks a window: 58 ms */
#define AFTER 8000                               /* ms listened after the note */
#define NB (AFTER * 441 / (BLK * 10) + 1)
#define NW (NB / WIN)

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int32_t take_out[NB][BLK];               /* left channel, after the note */
static uint32_t ms;

static void go(int msec, int keep)
{
    static int32_t out[2 * BLK];
    int blocks = msec * 441 / (BLK * 10) + 1, i, j;
    for (i = 0; i < blocks; i++) {
        h1_render(out, BLK);
        if (keep >= 0 && keep + i < NB)
            for (j = 0; j < BLK; j++)
                take_out[keep + i][j] = out[2 * j];
        if (i % 3 == 0)
            h1_frame(ms += 17);
    }
}

static void take(int algo, int drone, int gap)
{
    h1_init(44100);
    h1_control_set(H1_ENC_PRESET, BARD);
    h1_control_set(H1_ENC_ALGO, algo);
    h1_control_set(H1_ENC_K1, drone);
    go(1300, -1);
    h1_key(12, 1);
    go(600, -1);
    h1_key(12, 0);
    if (gap) {                                   /* a phrase, a short rest, and another phrase */
        go(gap, -1);
        h1_key(14, 1);
        go(600, -1);
        h1_key(14, 0);
    }
    go(AFTER, 0);
}

static void taken(int algo, int drone, int gap, int32_t (*into)[BLK])
{
    int fd[2];
    pid_t pid;
    size_t got = 0, want = sizeof take_out;
    fflush(stdout);
    if (pipe(fd) != 0 || (pid = fork()) < 0) {
        printf("FAIL: no process for a take\n");
        exit(1);
    }
    if (pid == 0) {
        const char *p = (const char *)take_out;
        size_t put = 0;
        close(fd[0]);
        take(algo, drone, gap);
        while (put < want) {
            ssize_t w = write(fd[1], p + put, want - put);
            if (w <= 0) _exit(1);
            put += (size_t)w;
        }
        _exit(0);
    }
    close(fd[1]);
    while (got < want) {
        ssize_t r = read(fd[0], (char *)into + got, want - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    close(fd[0]);
    waitpid(pid, 0, 0);
    if (got != want) {
        printf("FAIL: a take did not come back\n");
        exit(1);
    }
}

static int32_t up[NB][BLK], down[NB][BLK];
static double win_db(int w)                      /* the drone's level in window w, dB of full scale */
{
    double e = 0.0;
    int b, j;
    for (b = w * WIN; b < (w + 1) * WIN; b++)
        for (j = 0; j < BLK; j++) {
            double d = ((double)up[b][j] - (double)down[b][j]) / 8388608.0;
            e += d * d;
        }
    e /= (double)(WIN * BLK);
    return e > 1e-14 ? 10.0 * log10(e) : -140.0;
}

int main(void)
{
    int algo, gap, w, nalgo;
    h1_init(44100);
    h1_control_set(H1_ENC_PRESET, BARD);
    for (nalgo = 0; nalgo < 16 && h1_algo_name(BARD, nalgo)[0]; nalgo++)
        ;
    for (algo = 0; algo < nalgo; algo++)
        for (gap = 0; gap <= 500; gap += 500) {
            double db[NW], top = -140.0, worst = 0.0;
            int worst_at = 0, last_loud = 0;
            taken(algo, 100, gap, up);
            taken(algo, 0, gap, down);
            for (w = 0; w < NW; w++) {
                db[w] = win_db(w);
                if (db[w] > top) top = db[w];
                if (db[w] > -75.0) last_loud = w;
            }
            /* no window falls more than 3 dB under the one before while the drone can be heard. The
             * recordings and the wheel wobble by about 2 dB a window of their own; the cut it had fell by
             * 10 dB a window */
            for (w = 1; w < NW; w++)
                if (db[w - 1] > top - 50.0 && db[w - 1] - db[w] > worst) {
                    worst = db[w - 1] - db[w];
                    worst_at = w;
                }
            CHECK(top > -50.0, "%s%s: no drone heard (loudest %.1f dB)", h1_algo_name(BARD, algo),
                  gap ? " (two phrases)" : "", top);
            CHECK(worst <= 3.0, "%s%s: the drone falls %.1f dB in 58 ms, %d ms after the note",
                  h1_algo_name(BARD, algo), gap ? " (two phrases)" : "", worst, worst_at * WIN * BLK * 10 / 441);
            /* it is still there a second after the last note, and gone in the end */
            CHECK(db[1000 * 441 / (WIN * BLK * 10)] > top - 6.0, "%s%s: the drone did not hold (%.1f dB under)",
                  h1_algo_name(BARD, algo), gap ? " (two phrases)" : "", top - db[1000 * 441 / (WIN * BLK * 10)]);
            CHECK(db[NW - 1] < top - 40.0, "%s%s: the drone is still going 8 s on (%.1f dB under)",
                  h1_algo_name(BARD, algo), gap ? " (two phrases)" : "", top - db[NW - 1]);
            printf("  %-8s%s held %.1f dB at 1 s, steepest fall %.2f dB a window, heard until %d ms\n",
                   h1_algo_name(BARD, algo), gap ? " x2" : "   ", db[1000 * 441 / (WIN * BLK * 10)] - top, worst,
                   last_loud * WIN * BLK * 10 / 441);
        }
    printf("drone: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
