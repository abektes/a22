/* DICE: EDIT + LFO rolls the four knobs of the algorithm in hand, EDIT + ENV swaps back. Run by
 * tests/run_tests.sh, on the core alone, its panel worked as a hand would.
 *
 *   rolls     every algorithm of every engine, rolled 400 times: each knob within its span of the
 *             algorithm's home, a home of 0 stays 0, nothing past 0..100, and the rolls differ
 *   panel     EDIT + LFO moves the knobs and opens no page; EDIT + ENV puts them back exactly and again
 *             to the roll; another algorithm has nothing to swap; the knobs go out as CC 20-23 and are
 *             recorded on the tape like a hand's; LFO and ENV alone still open their pages
 *   sound     every algorithm rolled DICE_SOUND times stays within 6 dB of how loud it is at home
 *             (run with an argument "print" for the numbers per algorithm) */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../core/internal.h"
#include "../core/h1.h"

static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

static uint32_t t_ms;
static double energy;
static int cc_n[4], cc_v[4];
static void mo(uint8_t s, uint8_t a, uint8_t b)
{
    if ((s & 0xF0) == 0xB0 && a >= 20 && a <= 23) { cc_n[a - 20]++; cc_v[a - 20] = b; }
}
static void go(int msec)
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i, j;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        for (j = 0; j < 512; j++) energy += (double)out[j] * (double)out[j];
        if (i % 3 == 0) h1_frame(t_ms += 17);
    }
}
static void press(int b) { h1_button(b, 1); go(30); h1_button(b, 0); go(30); }
static void edit_with(int b)
{
    h1_button(H1_BTN_EDIT, 1); go(30);
    h1_button(b, 1); go(30); h1_button(b, 0); go(30);
    h1_button(H1_BTN_EDIT, 0); go(30);
}
static void fresh(void)
{
    int k;
    for (k = 0; k < H1_NKEYS; k++) h1_key(k, 0);
    h1_init(44100);
    h1_two_cores(0, 0);
    h1_midi_out(mo);
    go(60);
    memset(cc_n, 0, sizeof cc_n);
}
static int same4(const uint8_t *a, const uint8_t *b) { return !memcmp(a, b, 4); }

static void rolls(void)
{
    int i, a, k, n;
    uint32_t rng = 12345u;
    for (i = 0; i < NINST; i++)
        for (a = 0; a < NALGO; a++) {
            uint8_t home[4], r[4], lo[4], hi[4];
            int bad = 0, zero_moved = 0, varied = 0, movable = 0;
            knobs_home(i, a, home);
            for (k = 0; k < 4; k++) { lo[k] = 100; hi[k] = 0; movable += home[k] != 0; }
            for (n = 0; n < 400; n++) {
                dice_roll(i, a, &rng, r);
                for (k = 0; k < 4; k++) {
                    if (r[k] > 100 || (r[k] > home[k] ? r[k] - home[k] : home[k] - r[k]) > 30) bad++;
                    if (home[k] == 0 && r[k] != 0) zero_moved++;
                    if (r[k] < lo[k]) lo[k] = r[k];
                    if (r[k] > hi[k]) hi[k] = r[k];
                }
            }
            for (k = 0; k < 4; k++) varied += hi[k] > lo[k];
            CHECK(!bad, "a roll stays near home and inside 0..100");
            CHECK(!zero_moved, "a knob whose home is 0 stays 0");
            CHECK(varied == movable, "every knob not at 0 moves over 400 rolls");
            if (bad || zero_moved || varied != movable)
                printf("  engine %s algorithm %s\n", insts[i]->name, insts[i]->algo[a]);
        }
}

static int take_with_roll(int roll)
{
    fresh();
    press(H1_BTN_REC);
    h1_key(12, 1); go(200);
    if (roll) edit_with(H1_BTN_LFO);
    else go(120);
    go(200); h1_key(12, 0); go(100);
    press(H1_BTN_REC);
    go(100);
    return eng_loop_used();
}

static void panel(void)
{
    uint8_t home[4], rolled[4];
    const char *t;
    int k, plain, rolled_n;
    fresh();
    h1_control_set(H1_ENC_PRESET, 3);            /* ANALOG */
    h1_control_set(H1_ENC_ALGO, 5);              /* ACID */
    go(60);
    memcpy(home, KNOBS_NOW(st.inst), 4);
    memset(cc_n, 0, sizeof cc_n);
    edit_with(H1_BTN_LFO);
    memcpy(rolled, KNOBS_NOW(st.inst), 4);
    CHECK(!same4(home, rolled), "EDIT + LFO moves the knobs");
    CHECK(ui_page() == PG_HOME, "EDIT + LFO opens no page");
    t = h1_toast_now();
    CHECK(!strcmp(t, "DICE ACID"), "the toast names the roll and the algorithm");
    for (k = 0; k < 4; k++)
        if (rolled[k] != home[k])
            CHECK(cc_n[k] == 1 && cc_v[k] == (rolled[k] * 127 + 50) / 100, "a rolled knob goes out as its CC");

    edit_with(H1_BTN_ENV);
    CHECK(same4(KNOBS_NOW(st.inst), home), "EDIT + ENV puts the knobs back exactly");
    CHECK(!strcmp(h1_toast_now(), "BEFORE DICE"), "and says so");
    CHECK(ui_page() == PG_HOME, "EDIT + ENV opens no page");
    edit_with(H1_BTN_ENV);
    CHECK(same4(KNOBS_NOW(st.inst), rolled), "EDIT + ENV again: the roll");
    CHECK(!strcmp(h1_toast_now(), "AFTER DICE"), "and says so");

    edit_with(H1_BTN_LFO);
    CHECK(!same4(KNOBS_NOW(st.inst), rolled), "a second roll differs from the first");
    edit_with(H1_BTN_ENV);
    CHECK(same4(KNOBS_NOW(st.inst), rolled), "EDIT + ENV goes back to before the latest roll");

    h1_control_set(H1_ENC_ALGO, 6);              /* BRASS: nothing rolled here */
    go(60);
    memcpy(home, KNOBS_NOW(st.inst), 4);
    edit_with(H1_BTN_ENV);
    CHECK(same4(KNOBS_NOW(st.inst), home), "another algorithm has nothing to swap");
    CHECK(!strcmp(h1_toast_now(), "NO DICE TO UNDO"), "and says so");

    /* the roll is a hand on the knobs: recorded while the tape records (a take with it holds more) */
    plain = take_with_roll(0);
    rolled_n = take_with_roll(1);
    CHECK(plain >= 2 && rolled_n > plain, "the take holds the rolled knobs as well as the note");

    fresh();
    press(H1_BTN_LFO);
    CHECK(ui_page() == PG_LFO, "LFO alone still opens its page");
    press(H1_BTN_ENV);
    CHECK(ui_page() == PG_ENV, "ENV alone still opens its page");
}

/* ---- sound: a three-note chord, 1.2 s held, 0.6 s let go ---- */
#define DICE_SOUND 12
static double take(int i, int a, const uint8_t *k)
{
    static const int K[3] = {7, 11, 14};
    int n;
    fresh();
    h1_control_set(H1_ENC_PRESET, i);
    h1_control_set(H1_ENC_ALGO, a);
    if (k) {
        h1_control_set(H1_ENC_K1, k[0]);
        h1_control_set(H1_ENC_K2, k[1]);
        h1_control_set(H1_ENC_K3, k[2]);
        h1_control_set(H1_ENC_K4, k[3]);
    }
    go(400);                                     /* (the knobs eased to where they were put) */
    energy = 0.0;
    for (n = 0; n < 3; n++) h1_key(K[n], 1);
    go(1200);
    for (n = 0; n < 3; n++) h1_key(K[n], 0);
    go(600);
    return energy;
}

static void sound(int print)
{
    int i, a, n;
    uint32_t rng = 777u;
    double worst_lo = 0.0, worst_hi = 0.0;
    for (i = 0; i < NINST; i++)
        for (a = 0; a < NALGO; a++) {
            double home = take(i, a, 0), lo = 99.0, hi = -99.0;
            CHECK(home > 1e9, "the algorithm is heard at home");
            for (n = 0; n < DICE_SOUND; n++) {
                uint8_t r[4];
                double e, db;
                dice_roll(i, a, &rng, r);
                e = take(i, a, r);
                db = 10.0 * log10((e + 1.0) / (home + 1.0));
                if (db < lo) lo = db;
                if (db > hi) hi = db;
                if (!(db > -6.0 && db < 6.0))
                    printf("  %s %s rolled %d %d %d %d: %+.1f dB from home\n", insts[i]->name,
                           insts[i]->algo[a], r[0], r[1], r[2], r[3], db);
                CHECK(db > -6.0 && db < 6.0, "a roll stays within 6 dB of home");
            }
            if (lo < worst_lo) worst_lo = lo;
            if (hi > worst_hi) worst_hi = hi;
            if (print) printf("%-9s %-9s %+5.1f .. %+5.1f dB\n", insts[i]->name, insts[i]->algo[a], lo, hi);
        }
    printf("dice sound: %d algorithms x %d rolls, from %+.1f to %+.1f dB of home\n", NINST * NALGO, DICE_SOUND,
           worst_lo, worst_hi);
}

int main(int argc, char **argv)
{
    int print = argc > 1 && !strcmp(argv[1], "print");
    rolls();
    panel();
    sound(print);
    printf("dice: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
