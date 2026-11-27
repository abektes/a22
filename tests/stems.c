/* The USB audio input's stems on the core alone (h1_stems): T1 to T4 on the four pairs after the mix, each track on its
 * own pair and nowhere else, a muted one silent; the mix the same to the bit with stems taken or not; nothing written
 * and no stem work while nobody records; and with the master bus flat and the effect bypassed, the four stems adding up
 * to the mix (its 18 Hz filter put on them here), the limiter's gain on them too, which a loud chord brings in.
 * The port's frames come in runs of 37 (a ring that wraps at an odd place). Run by tests/run_tests.sh;
 * tests/stems.txt and tools/a22_stems.py check the same through fm1-base's simulator, MASTER included. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../core/internal.h"
#include "../core/h1.h"

static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

#define BLK 256
#define RUN 37u
static int16_t frames[BLK][10];                  /* one render's frames as the port keeps them: 0-1 the mix's, 2.. ours */
static int nobody, asked;
static int16_t *get(uint32_t off, uint32_t *run)
{
    asked++;
    if (nobody || off >= BLK) {
        *run = 0;
        return 0;
    }
    *run = RUN - off % RUN;                      /* (the ring wraps every 37 frames: ask again from there) */
    if (*run > BLK - off)
        *run = BLK - off;
    return &frames[off][2];
}

static int32_t out[2 * BLK];
static uint32_t ms;

/* one render; stems_peak[p]: the loudest sample on pair p (T1..T4) */
static int32_t stems_peak[4];
static void render(void)
{
    int i, k;
    memset(frames, 0, sizeof frames);
    h1_render(out, BLK);
    for (i = 0; i < BLK; i++)
        for (k = 0; k < 8; k++) {
            int32_t a = frames[i][2 + k] < 0 ? -frames[i][2 + k] : frames[i][2 + k];
            if (a > stems_peak[k >> 1])
                stems_peak[k >> 1] = a;
        }
    if ((ms += 6) % 23 < 6)
        h1_frame(ms);
}
static void go(int msec)
{
    int n = msec * 441 / (BLK * 10) + 1;
    while (n--)
        render();
}
static void peaks_reset(void) { memset(stems_peak, 0, sizeof stems_peak); }

static void track(int d)                         /* EDIT held, PRESETS turned, EDIT let go */
{
    h1_button(H1_BTN_EDIT, 1);
    h1_encoder(H1_ENC_PRESET, d);
    h1_button(H1_BTN_EDIT, 0);
}

static void fresh(int stems)
{
    h1_init(44100);
    h1_two_cores(0, 0);
    h1_stems(stems ? get : 0, 10);
    nobody = asked = 0;
    ms = 0;
    peaks_reset();
}

/* each track alone: its pair only, the other three exactly 0; then muted, its pair silent too */
static void each_on_its_own(void)
{
    int t, p;
    for (t = 0; t < 4; t++) {
        fresh(1);
        track(t);
        go(100);
        peaks_reset();
        h1_key(12, 1);
        go(300);
        h1_key(12, 0);
        go(200);
        for (p = 0; p < 4; p++) {
            char what[80];
            snprintf(what, sizeof what, "T%d alone: %s pair %d (peak %d)", t + 1, p == t ? "heard on" : "silent on",
                     p + 1, (int)stems_peak[p]);
            CHECK(p == t ? stems_peak[p] > 200 : stems_peak[p] == 0, what);
        }
        st.trk[t].mute = 1;                      /* (as EDIT + SEL sets it) */
        go(400);                                 /* (the volume's smoothing comes down to nothing) */
        peaks_reset();
        h1_key(12, 1);
        go(300);
        h1_key(12, 0);
        CHECK(stems_peak[t] == 0, "a muted track's pair is silent");
    }
}

/* the scenario for the next two: a note on each track in turn, their tails overlapping, then a chord on T3 */
static void scenario(int32_t *mix, int nblk, int16_t (*st8)[8])
{
    int b = 0, t, i, k;
    static const int keys[10] = {0, 2, 4, 7, 9, 12, 14, 16, 19, 21};
    for (t = 0; t < 4; t++) {
        if (t)
            track(1);
        h1_key(12 + 2 * t, 1);
        for (i = 0; i < 40 && b < nblk; i++, b++) {
            render();
            memcpy(mix + 2 * BLK * b, out, sizeof out);
            if (st8)
                for (k = 0; k < BLK; k++)
                    memcpy(st8[b * BLK + k], &frames[k][2], 16);
        }
        h1_key(12 + 2 * t, 0);
    }
    track(-1);                                   /* (T3: ANALOG holds its notes) */
    for (k = 0; k < 10; k++)
        h1_key(keys[k], 1);
    for (; b < nblk; b++) {
        if (b == nblk - 30)
            for (k = 0; k < 10; k++)
                h1_key(keys[k], 0);
        render();
        memcpy(mix + 2 * BLK * b, out, sizeof out);
        if (st8)
            for (k = 0; k < BLK; k++)
                memcpy(st8[b * BLK + k], &frames[k][2], 16);
    }
}

#define NB 260
static int32_t mix_on[2 * BLK * NB], mix_off[2 * BLK * NB];
static int16_t stems8[BLK * NB][8];

/* the mix, effects and master bus as they come, is the same to the bit with the stems taken or not */
static void mix_unchanged(void)
{
    fresh(1);
    scenario(mix_on, NB, 0);
    fresh(0);
    scenario(mix_off, NB, 0);
    CHECK(memcmp(mix_on, mix_off, sizeof mix_on) == 0, "the mix is the same to the bit with stems taken and without");
}

/* nobody recording: the port asked once a render, nothing written, and the mix the same again */
static void nobody_records(void)
{
    int i, k, b, written = 0;
    fresh(1);
    nobody = 1;
    h1_key(12, 1);
    for (b = 0; b < 100; b++) {
        for (i = 0; i < BLK; i++)
            for (k = 0; k < 10; k++)
                frames[i][k] = 0x5A5A;
        h1_render(out, BLK);
        for (i = 0; i < BLK; i++)
            for (k = 0; k < 10; k++)
                written += frames[i][k] != 0x5A5A;
    }
    CHECK(written == 0, "nobody recording: no stem written");
    CHECK(asked == 100, "nobody recording: the port asked once a render, no more");
}

/* With the master bus flat (no EQ, no drive) and the effect bypassed, the mix is the four tracks, the 18 Hz filter,
 * the limiter's gain and its soft top above 0.6. The stems with that filter put on them here add up to the mix where it
 * is below 0.55 (the int16 rounding of four stems and the filter taken after the gain instead of before are what is
 * left); a loud chord on T3 brings the limiter in. */
static void stems_add_up(void)
{
    int i, b, used = 0;
    double sub = 0.0, k = 2.0 * 3.14159265358979 * 18.0 / 44100.0, err = 0.0, sig = 0.0, peak = 0.0;
    fresh(1);
    st.master[0] = st.master[1] = st.master[2] = 50;
    st.master[3] = 0;
    st.fx_on = 0;
    for (i = 0; i < 4; i++)
        st.trk[i].vol = 100;
    scenario(mix_on, NB, stems8);
    for (b = 0; b < NB * BLK; b++) {
        double s = 0.0, m = (double)mix_on[2 * b] * (1.0 / 256.0), y;
        for (i = 0; i < 8; i += 2)
            s += stems8[b][i];
        sub += k * (s - sub);
        y = s - sub;
        if (fabs(m) > peak)
            peak = fabs(m);
        if (fabs(m) > 0.55 * 29491.2)
            continue;
        err += (m - y) * (m - y);
        sig += m * m;
        used++;
    }
    printf("stems: the mix's peak %.2f of full scale; the stems off the mix by %.2f%% (%d frames)\n",
           peak / 32768.0, 100.0 * sqrt(err / sig), used);
    CHECK(peak > 0.62 * 29491.2, "the chord is loud enough for the limiter");
    CHECK(sqrt(err / sig) < 0.02, "the stems add up to the mix (filtered as it is), the limiter's gain on them too");
}

int main(void)
{
    each_on_its_own();
    mix_unchanged();
    nobody_records();
    stems_add_up();
    printf("stems: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
