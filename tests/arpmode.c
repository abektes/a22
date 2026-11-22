/* The SEQ / ARP page's MODE: every mode can be reached, and SHAKE can be left again. Run by
 * tests/run_tests.sh. Issue #1 ("ARP stuck in GRAVITY"): SHAKE's settings once took all four columns, so the
 * column that had just turned SHAKE on became GRAV, and turning it back only took GRAV to 0. Now K1 is MODE
 * in every mode, and SHAKE's settings take two subpages: 1/2 MODE GRAV SPIN BOUNCE, 2/2 MODE CAGE.
 *
 *   simulator   the web simulator's sliders: h1_control / h1_control_set (port/rfm1/rfm1.c, rfm1_sim1_ctl*)
 *     1  K1 to SHAKE, then K1 back down: the mode leaves SHAKE (what the reporter did), GRAV untouched
 *     2  some slider on the page leaves SHAKE
 *     3  the ALGORITHM slider on the ARP page is what the encoder there is: SEQ mode, not PRESET
 *   device      the FM-1's encoders: h1_encoder
 *     4  ALGORITHM turned on the ARP page leaves SHAKE
 *     5  K1 turned one detent at a time from OFF reaches every mode, up to CHORD, and back to OFF
 *   SHAKE's two subpages
 *     6  1/2 MODE GRAV SPIN BOUNCE and 2/2 MODE CAGE each set their own setting, 2/2's empty columns set
 *        nothing; leaving SHAKE from 2/2 leaves the page one subpage again, and SHAKE comes back on 1/2 */
#include <stdio.h>
#include <string.h>
#include "../core/internal.h"
#include "../core/h1.h"

static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

static const char *const MODE_N[NARPMODE] = {"OFF", "RIFF", "SHAKE", "UP", "DOWN", "UPDN", "RAND", "CHORD"};

static uint32_t t_ms;
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
static void tap(int b) { h1_button(b, 1); go(40); h1_button(b, 0); go(40); }
static int mode(void) { return st.ip[st.inst].arp[0]; }
static void fresh(void)
{
    int k;
    for (k = 0; k < H1_NKEYS; k++)
        h1_key(k, 0);
    h1_init(44100);
    h1_two_cores(0, 0);
    go(100);
    tap(H1_BTN_ARP);                             /* the SEQ / ARP page */
}
static int verbose;
static void show(int enc, const char *as)
{
    int lo, hi, v;
    const char *nm, *tx;
    v = h1_control(enc, &lo, &hi, &nm, &tx);
    if (verbose) printf("     %s: %s = %d (%s), %d..%d; mode %s, GRAV %d\n", as, nm, v, tx, lo, hi,
           mode() < NARPMODE ? MODE_N[mode()] : "?", st.shake[0]);
}

static const char *col(int k)                    /* a column's name, as the screen and the simulator have it */
{
    const char *nm = 0;
    h1_control(k, 0, 0, &nm, 0);
    return nm ? nm : "?";
}
static int cols_are(const char *a, const char *b, const char *c, const char *d)
{
    return !strcmp(col(H1_ENC_K1), a) && !strcmp(col(H1_ENC_K2), b) && !strcmp(col(H1_ENC_K3), c) &&
           !strcmp(col(H1_ENC_K4), d);
}
static int sub_now(int *n)                       /* the page's subpage, and how many it has */
{
    int lo, hi, v = h1_control(H1_ENC_SELECT, &lo, &hi, 0, 0);
    if (n) *n = hi + 1;
    return v;
}

int main(int argc, char **argv)
{
    verbose = argc > 1;
    /* ---- the simulator's sliders ---- */
    {
        int lo, hi, v, k, left;
        const char *nm, *tx;
        fresh();
        CHECK(h1_page() == PG_ARP, "ARP opens the SEQ / ARP page");
        CHECK(mode() == ARP_OFF, "a new a22's mode is OFF");
        h1_control(H1_ENC_K1, &lo, &hi, &nm, &tx);
        CHECK(!strcmp(nm, "MODE"), "K1's slider is MODE");

        h1_control_set(H1_ENC_K1, ARP_SHAKE);
        go(100);
        CHECK(mode() == ARP_SHAKE, "K1's slider set to 2: SHAKE");
        show(H1_ENC_K1, "K1 now");

        h1_control_set(H1_ENC_K1, ARP_OFF);      /* the slider dragged back down */
        go(100);
        show(H1_ENC_K1, "K1 dragged to 0");
        CHECK(mode() != ARP_SHAKE, "1  K1's slider dragged back down leaves SHAKE");
        CHECK(st.shake[0] != 0 || mode() != ARP_SHAKE, "1  ...and does not take GRAV to 0 instead (the screenshot)");

        /* 2: any slider on this page, any value, that gets out of SHAKE */
        fresh();
        h1_control_set(H1_ENC_K1, ARP_SHAKE);
        go(100);
        left = 0;
        for (k = 0; k < H1_NENC && !left; k++) {
            if (k == H1_ENC_PRESET || k == H1_ENC_SELECT) continue;   /* (they leave the page) */
            h1_control(k, &lo, &hi, &nm, &tx);
            for (v = lo; v <= hi && !left; v++) {
                h1_control_set(k, v);
                if (mode() != ARP_SHAKE) left = 1;
            }
        }
        CHECK(left, "2  some slider on the SEQ / ARP page gets out of SHAKE");

        /* 3: ALGORITHM on this page is SEQ mode on the FM-1 (h1_encoder); the simulator should say and do so */
        fresh();
        st.ip[st.inst].algo = 5;               /* (not ARP_UP, so a slider that sets the preset shows) */
        v = h1_control(H1_ENC_ALGO, &lo, &hi, &nm, &tx);
        if (verbose) printf("     ALGORITHM slider on the ARP page: %s = %d (%s), %d..%d\n", nm, v, tx, lo, hi);
        CHECK(v == mode() && lo == 0 && hi == NARPMODE - 1, "3  the ALGORITHM slider shows the SEQ mode and its range");
        h1_control_set(H1_ENC_ALGO, ARP_UP);
        go(100);
        CHECK(mode() == ARP_UP, "3  the ALGORITHM slider sets the SEQ mode, as the encoder does");
        CHECK(st.ip[st.inst].algo == 5, "3  ...and leaves the preset alone");
    }

    /* ---- the FM-1's encoders ---- */
    {
        int seen[NARPMODE], i, n;
        fresh();
        h1_encoder(H1_ENC_ALGO, 2);              /* OFF, RIFF, SHAKE */
        go(100);
        CHECK(mode() == ARP_SHAKE, "ALGORITHM turned two on the ARP page: SHAKE");
        h1_encoder(H1_ENC_ALGO, 1);
        go(100);
        CHECK(mode() == ARP_UP, "4  ALGORITHM turned on in SHAKE: UP");
        h1_encoder(H1_ENC_ALGO, -1);
        h1_encoder(H1_ENC_ALGO, -1);
        go(100);
        CHECK(mode() == ARP_RIFF, "4  ALGORITHM turned back past SHAKE: RIFF");

        fresh();
        memset(seen, 0, sizeof seen);
        seen[mode()] = 1;
        for (i = 0; i < 4 * NARPMODE; i++) {     /* far more detents than there are modes */
            h1_encoder(H1_ENC_K1, 1);
            go(30);
            if (mode() < NARPMODE) seen[mode()] = 1;
        }
        show(H1_ENC_K1, "K1 after 32 detents up");
        for (i = n = 0; i < NARPMODE; i++) {
            n += seen[i];
            if (!seen[i]) printf("     K1 never reached %s\n", MODE_N[i]);
        }
        CHECK(n == NARPMODE, "5  K1 turned up from OFF reaches every mode, up to CHORD");
        for (i = 0; i < 4 * NARPMODE; i++) {
            h1_encoder(H1_ENC_K1, -1);
            go(30);
        }
        CHECK(mode() == ARP_OFF, "5  ...and turned back down reaches OFF again");
    }

    /* ---- SHAKE's two subpages ---- */
    {
        int n, lo, hi;
        uint8_t was[4];
        fresh();
        CHECK(cols_are("MODE", "RATE", "RANGE", "GATE"), "OFF: MODE RATE RANGE GATE");
        h1_control_set(H1_ENC_K1, ARP_SHAKE);
        go(100);
        CHECK(sub_now(&n) == 0 && n == 2, "6  SHAKE: the page has two subpages, on the first");
        CHECK(cols_are("MODE", "GRAV", "SPIN", "BOUNCE"), "6  SHAKE 1/2: MODE GRAV SPIN BOUNCE");
        h1_control_set(H1_ENC_K2, 70);
        h1_control_set(H1_ENC_K3, 20);
        h1_control_set(H1_ENC_K4, 90);
        CHECK(st.shake[0] == 70 && st.shake[1] == 20 && st.shake[2] == 90 && st.shake[3] == 50,
              "6  K2 K3 K4 set GRAV SPIN BOUNCE, and only them");
        CHECK(mode() == ARP_SHAKE, "6  ...and the mode stays SHAKE");

        h1_encoder(H1_ENC_SELECT, 1);
        go(100);
        CHECK(h1_page() == PG_ARP && sub_now(0) == 1, "6  SELECT: SHAKE 2/2, still on the page");
        CHECK(cols_are("MODE", "CAGE", "", ""), "6  SHAKE 2/2: MODE CAGE");
        h1_control_set(H1_ENC_K2, 30);
        CHECK(st.shake[3] == 30 && st.shake[0] == 70, "6  K2 sets CAGE");
        memcpy(was, st.shake, 4);
        h1_control(H1_ENC_K3, &lo, &hi, 0, 0);
        CHECK(lo == hi, "6  an empty column has nothing for a slider to set");
        h1_encoder(H1_ENC_K3, 5);
        h1_encoder(H1_ENC_K4, -5);
        h1_control_set(H1_ENC_K3, 40);
        CHECK(!memcmp(was, st.shake, 4) && mode() == ARP_SHAKE, "6  K3 and K4 on 2/2 change nothing");

        h1_encoder(H1_ENC_K1, 1);                /* SHAKE -> UP, from 2/2 */
        go(100);
        CHECK(mode() == ARP_UP, "6  K1 on 2/2 is still MODE: UP");
        CHECK(sub_now(&n) == 0 && n == 1, "6  out of SHAKE: one subpage again");
        CHECK(cols_are("MODE", "RATE", "RANGE", "GATE"), "6  UP: MODE RATE RANGE GATE");
        h1_encoder(H1_ENC_ALGO, -1);             /* back to SHAKE */
        go(100);
        CHECK(mode() == ARP_SHAKE && sub_now(0) == 0, "6  back to SHAKE: on 1/2");
        CHECK(st.shake[0] == 70 && st.shake[3] == 30, "6  SHAKE kept its settings");
        h1_encoder(H1_ENC_SELECT, 1);
        h1_encoder(H1_ENC_SELECT, 1);
        CHECK(h1_page() == PG_ARP && sub_now(0) == 0, "6  SELECT twice: round to 1/2");
    }

    printf("arpmode: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
