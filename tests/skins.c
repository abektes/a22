/* SKINS, engine 14: two recorded kits in four treatments each. Run by tests/run_tests.sh.
 *
 *   every pad      all 12 drums on all 8 algorithms sound, stay inside the limiter, and are silent again
 *                  a few seconds after the key is let go, DECAY at its top (the cymbals go round a loop)
 *   the kits       STUDIO and ORCH play different recordings on the same key
 *   the choke      the closed hat cuts the open hat short (STUDIO), the short clash the long one (ORCH)
 *   ROOM           at 0 a centred kick is centred; turned up, the reflections spread it, and the snare's
 *                  tail grows (at a track volume under the limiter)
 *   treatments     with the knobs alike, TIGHT is shorter than STUDIO, VINTAGE darker, LIVE longer, and
 *                  every one of the eight differs from every other on the snare
 *   PUNCH          the ENV page's, off by default: turned up, the kick's start is harder and its body as it
 *                  was, a hat is untouched, and nothing reaches the limiter's ceiling
 *
 * Each take is made in a process of its own (as tests/basics.c): h1_init does not wind the engine's
 * clocks back. Key 7 is middle C, so key 7 + d plays drum d (C kick .. B cymbal). */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "../core/internal.h"
#include "../core/h1.h"

#define SKINS 13
static uint32_t ms;
static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

typedef struct Heard { double mid, side, slope, late; int32_t peak; uint32_t hash, n; } Heard;
static Heard heard;
static int32_t last;
static int late_from = -1;                       /* (heard.late: the energy after this many samples) */

static void go(int msec)
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i, j;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        for (j = 0; j < 512; j += 2) {
            double l = out[j], r = out[j + 1], m = (l + r) * 0.5, s = (l - r) * 0.5, d = m - (double)last;
            int32_t a = out[j] < 0 ? -out[j] : out[j], b = out[j + 1] < 0 ? -out[j + 1] : out[j + 1];
            heard.mid += m * m;
            heard.side += s * s;
            heard.slope += d * d;
            if (late_from >= 0 && heard.n >= (uint32_t)late_from) heard.late += m * m;
            if (a > heard.peak) heard.peak = a;
            if (b > heard.peak) heard.peak = b;
            heard.hash = heard.hash * 31u + (uint32_t)out[j] * 7u + (uint32_t)out[j + 1];
            last = (int32_t)m;
            heard.n++;
        }
        if (i % 3 == 0)
            h1_frame(ms += 17);
    }
}
static void listen(void) { memset(&heard, 0, sizeof heard); late_from = -1; }

static Heard taken(Heard (*fn)(int, int), int a, int b)
{
    int fd[2];
    Heard h;
    pid_t pid;
    fflush(stdout);
    if (pipe(fd) != 0 || (pid = fork()) < 0) {
        printf("FAIL: no process for a take\n");
        exit(1);
    }
    if (pid == 0) {
        h = fn(a, b);
        _exit(write(fd[1], &h, sizeof h) == (ssize_t)sizeof h ? 0 : 1);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != (ssize_t)sizeof h) {
        printf("FAIL: a take did not come back\n");
        exit(1);
    }
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}

static void skins(int algo)
{
    h1_init(44100);
    st.fx_on = 0;                                /* (no effect: the drums alone) */
    h1_control_set(H1_ENC_PRESET, SKINS);
    h1_control_set(H1_ENC_ALGO, algo);
    go(300);
}

/* ---- one drum d on algorithm a, DECAY at its top: what it sounds like, then what is left 3 s on ---- */
static Heard pad(int a, int d)
{
    Heard h;
    skins(a);
    h1_control_set(H1_ENC_K2, 100);
    go(200);
    listen();
    h1_key(7 + d, 1);
    go(400);
    h1_key(7 + d, 0);
    go(400);
    h = heard;
    go(2600);
    listen();
    go(400);
    h.late = heard.mid;                          /* (here: the last 400 ms) */
    return h;
}

/* ---- the choke: open drum o alone (c < 0), or o then closed drum c 30 ms on, or c alone (o < 0) ---- */
static Heard choke(int ao, int c)
{
    int a = ao >> 8, o = (ao & 0xFF) == 0xFF ? -1 : (ao & 0xFF);
    skins(a);
    listen();
    late_from = (int)(0.25 * 44100);
    if (o >= 0) h1_key(7 + o, 1);
    go(30);
    if (c >= 0) h1_key(7 + c, 1);
    go(800);
    return heard;
}

/* ---- one drum d on algorithm a, ROOM at room (-1: as the algorithm has it) ---- */
#define SAME_KNOBS (1 << 16)
#define QUIET (1 << 17)
static Heard hit(int a, int dr)
{
    int d = dr & 0xFF, room = (dr >> 8) & 0xFF;
    skins(a);
    if (dr & SAME_KNOBS) {                       /* (every algorithm's knobs alike: the treatment alone) */
        h1_control_set(H1_ENC_K1, 50);
        h1_control_set(H1_ENC_K2, 50);
        h1_control_set(H1_ENC_K3, 60);
    }
    if (room <= 100) h1_control_set(H1_ENC_K4, room);
    if (dr & QUIET) st.trk[st.track].vol = 30;    /* (well under the limiter: what ROOM adds, not what it squeezes) */
    go(100);
    listen();
    late_from = (int)(0.15 * 44100);
    h1_key(7 + d, 1);
    go(150);
    h1_key(7 + d, 0);
    go(700);
    return heard;
}
#define AS_IS (255 << 8)

/* ---- one drum d on algorithm a with the ENV page's PUNCH at p (dp: d | p << 8 | LOUD): late is from 6 ms on ---- */
#define LOUD (1 << 16)
static Heard punched(int a, int dp)
{
    int d = dp & 0xFF, p = (dp >> 8) & 0xFF;
    skins(a);
    st.ip[SKINS].menv[3] = (uint8_t)p;
    if (!(dp & LOUD)) st.trk[st.track].vol = 30;
    go(100);
    listen();
    late_from = 265;
    h1_key(7 + d, 1);
    go(150);
    h1_key(7 + d, 0);
    go(300);
    return heard;
}

int main(void)
{
    int a, d, b;
    char what[160];

    {   /* every pad */
        int sounds = 1, inside = 1, quiet = 1;
        for (a = 0; a < 8; a++)
            for (d = 0; d < 12; d++) {
                Heard h = taken(pad, a, d);
                double rms = sqrt(h.mid / h.n), left = sqrt(h.late / (0.4 * 44100));
                if (rms < 300.0) { sounds = 0; printf("  %s pad %d: rms %.0f\n", h1_algo_name(SKINS, a), d, rms); }
                if (h.peak >= 0x7FFFFF) { inside = 0; printf("  %s pad %d: peak %d\n", h1_algo_name(SKINS, a), d, (int)h.peak); }
                if (left > 2.0) { quiet = 0; printf("  %s pad %d: %.1f rms left after 3 s\n", h1_algo_name(SKINS, a), d, left); }
            }
        CHECK(sounds, "every drum on every algorithm sounds");
        CHECK(inside, "and stays inside the limiter");
        CHECK(quiet, "and is silent again 3 s after its key is let go, DECAY at its top");
        printf("skins: 12 drums x 8 algorithms sound and end\n");
    }

    {   /* the kits */
        int differ = 1;
        for (d = 0; d < 12; d++)
            differ &= taken(hit, 0, d | AS_IS).hash != taken(hit, 4, d | AS_IS).hash;
        CHECK(differ, "STUDIO and ORCH play different drums on every key");
    }

    {   /* the choke: STUDIO's hats (F# 6 closes A# 10), ORCH's clash cymbals */
        for (a = 0; a < 8; a += 4) {
            Heard open = taken(choke, (a << 8) | 10, -1);
            Heard both = taken(choke, (a << 8) | 10, 6);
            Heard closed = taken(choke, (a << 8) | 0xFF, 6);
            snprintf(what, sizeof what, "%s: the open drum rings on alone (%.3g against the closed one's %.3g)",
                     h1_algo_name(SKINS, a), open.late, closed.late);
            CHECK(open.late > 3.0 * closed.late, what);
            snprintf(what, sizeof what, "%s: the closed drum cuts it short (%.3g, alone %.3g)",
                     h1_algo_name(SKINS, a), both.late, open.late);
            CHECK(both.late < 0.25 * open.late, what);
        }
    }

    {   /* ROOM, at a track volume the limiter leaves alone: the kick (pan 0) spreads, the snare's tail grows.
         * (The reflections keep no low end: a pitched kick's 80 Hz cancels against copies of itself 10 and
         * 17 ms late, so on the kick ROOM is width and air, not more level.) */
        Heard dry = taken(hit, 0, 0 | (0 << 8) | QUIET), wet = taken(hit, 0, 0 | (70 << 8) | QUIET);
        Heard sdry = taken(hit, 0, 2 | (0 << 8) | QUIET), swet = taken(hit, 0, 2 | (70 << 8) | QUIET);
        snprintf(what, sizeof what, "ROOM 0: a centred kick is centred (side %.3g of mid %.3g)", dry.side, dry.mid);
        CHECK(dry.side < 1e-5 * dry.mid, what);
        snprintf(what, sizeof what, "ROOM 70: the reflections spread it (side %.3g of mid %.3g)", wet.side, wet.mid);
        CHECK(wet.side > 1e-3 * wet.mid, what);
        snprintf(what, sizeof what, "ROOM 70: the snare's tail grows (%.3g against %.3g)", swet.late, sdry.late);
        CHECK(swet.late > 1.1 * sdry.late, what);
    }

    {   /* treatments, on the snare (D, drum 2), every algorithm's knobs alike (ROOM 0 but on LIVE and HALL) */
        Heard h[8];
        int apart = 1;
        for (a = 0; a < 8; a++)
            h[a] = taken(hit, a, 2 | SAME_KNOBS | ((a & 3) == 1 ? 60 << 8 : 0));
        for (a = 0; a < 8; a++)
            for (b = a + 1; b < 8; b++)
                if (h[a].hash == h[b].hash) { apart = 0; printf("  %s and %s are the same\n", h1_algo_name(SKINS, a), h1_algo_name(SKINS, b)); }
        CHECK(apart, "the eight algorithms all sound different on the snare");
        for (a = 0; a < 8; a += 4) {
            double dry_tail = h[a].late / h[a].mid, tight_tail = h[a + 2].late / h[a + 2].mid;
            double live_tail = h[a + 1].late / h[a + 1].mid;
            double dry_bright = h[a].slope / h[a].mid, old_bright = h[a + 3].slope / h[a + 3].mid;
            snprintf(what, sizeof what, "%s: TIGHT's tail is shorter (%.3g against %.3g)", h1_algo_name(SKINS, a + 2), tight_tail, dry_tail);
            CHECK(tight_tail < 0.6 * dry_tail, what);
            snprintf(what, sizeof what, "%s: LIVE's tail is longer (%.3g against %.3g)", h1_algo_name(SKINS, a + 1), live_tail, dry_tail);
            CHECK(live_tail > 1.3 * dry_tail, what);
            snprintf(what, sizeof what, "%s: VINTAGE is darker (%.3g against %.3g)", h1_algo_name(SKINS, a + 3), old_bright, dry_bright);
            CHECK(old_bright < 0.7 * dry_bright, what);
        }
    }

    {   /* PUNCH (the ENV page's), as KIT hears it: off by default; the kick's start harder, its body as it was;
         * a hat not at all; and nothing past the limiter on any algorithm */
        Heard k0 = taken(punched, 0, 0), k1 = taken(punched, 0, 0 | 100 << 8);
        Heard h0 = taken(punched, 0, 6), h1 = taken(punched, 0, 6 | 100 << 8);
        double e0 = k0.mid - k0.late, e1 = k1.mid - k1.late, body = k1.late / k0.late;
        int inside = 1;
        h1_init(44100);
        CHECK(st.ip[SKINS].menv[3] == 0, "SKINS starts with PUNCH at 0");
        snprintf(what, sizeof what, "PUNCH 100: the kick's first 6 ms are harder (%.3g against %.3g)", e1, e0);
        CHECK(e1 > 1.2 * e0, what);
        snprintf(what, sizeof what, "PUNCH 100: the kick's body is as it was (%.2f dB)", 10.0 * log10(body));
        CHECK(body > 0.89 && body < 1.12, what);
        CHECK(h0.hash == h1.hash, "PUNCH leaves the hat alone");
        for (a = 0; a < 8; a++)
            if (taken(punched, a, 0 | 100 << 8 | LOUD).peak >= 0x7FFFFF) {
                inside = 0;
                printf("  %s: the kick at PUNCH 100 reaches the limiter's ceiling\n", h1_algo_name(SKINS, a));
            }
        CHECK(inside, "PUNCH 100 stays inside the limiter on every algorithm");
    }

    printf("skins: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
