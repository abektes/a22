/* The saved state, version 5: room for 16 engines and 12 effects. Run by tests/run_tests.sh (from the
 * repository's top, for tests/state_v4.bin).
 *
 *   version 4   tests/state_v4.bin (written by 0.4.0's own core: tests/state_v4_gen.c) loads, from any
 *               address, with every table as it was saved; the new slots come up empty, effects as 8
 *   version 5   saved and loaded again, to the byte, from an odd address
 *   refused     a wrong size for its version, another version, another magic, a few bytes: nothing changes
 *   new slots   an engine or effect past what a state was saved with (ninst, nfx) comes up as new, the
 *               ones before it as saved: so adding an engine or effect needs no new version */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../core/internal.h"
#include "../core/h1.h"

static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

typedef struct V4 {                              /* version 4's layout (as core/h1.c's H1StateV4) */
    uint32_t magic;
    uint8_t version, inst, track;
    int8_t octave;
    uint8_t hud, keys[4], glo[4], seq[8], tape[4], fx_type, fx_on;
    uint8_t fx_p[8][4];
    uint8_t master[4], shake[4], riff_len, riff_root, riff[NRIFF], chord[4], chord_n, chord_mem[CHORD_MAX];
    uint8_t ninst, rsv[3];
    InstParams ip[12];
    uint8_t knob[12][NALGO][4];
    Track trk[NTRACK];
    Snap snap[NSNAP];
    uint8_t steps[NSTEP][NSNOTE], step_trk[NSTEP][NSNOTE], step_vel[NSTEP][NSNOTE];
} V4;

static uint8_t v4[4096], buf[8192], v5[4096];
static uint32_t v4_n;

static int zero(const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--) if (*b++) return 0;
    return 1;
}
#define SAME(field, off, n) (!memcmp((const uint8_t *)&st + offsetof(H1State, field), v4 + (off), (n)))

static void version4(void)
{
    int odd;
    for (odd = 0; odd < 4; odd++) {
        h1_init(44100);
        memcpy(buf + odd, v4, v4_n);
        CHECK(h1_state_load(buf + odd, v4_n) == 0, "a version 4 state loads, from any address");
        CHECK(SAME(magic, 0, offsetof(V4, version)) &&
              SAME(inst, offsetof(V4, inst), offsetof(V4, fx_p) - offsetof(V4, inst)),
              "everything before the effects but the version, as saved");
        CHECK(SAME(fx_p, offsetof(V4, fx_p), sizeof ((V4 *)0)->fx_p), "the eight effects' knobs, as saved");
        CHECK(SAME(master, offsetof(V4, master), offsetof(V4, ninst) + 1 - offsetof(V4, master)),
              "master, shake, riff, chord memory and the engine count, as saved");
        CHECK(SAME(ip, offsetof(V4, ip), sizeof ((V4 *)0)->ip), "the twelve engines' settings, as saved");
        CHECK(SAME(knob, offsetof(V4, knob), sizeof ((V4 *)0)->knob), "every algorithm's knobs, as saved");
        CHECK(SAME(trk, offsetof(V4, trk), sizeof(V4) - offsetof(V4, trk)),
              "the tracks, snapshots and steps, as saved");
        CHECK(zero(st.fx_p[8], sizeof st.fx_p - sizeof st.fx_p[0] * 8), "the new effect slots are empty");
        CHECK(zero(&st.ip[12], sizeof st.ip[0] * 4) && zero(st.knob[12], sizeof st.knob[0] * 4),
              "the new engine slots are empty");
        CHECK(st.nfx == NFX && st.version == H1_VERSION, "it is version 5 now, with its effects counted");
    }
    CHECK(h1_instrument() == 10 && h1_algorithm() == 10 % 8, "the engine and algorithm in hand, as saved");
}

static void version5(void)
{
    uint32_t n;
    const void *p;
    h1_init(44100);
    h1_state_load(v4, v4_n);
    p = h1_state(&n);
    CHECK(n == sizeof(H1State) && n == 2888u, "version 5 is 2888 bytes");
    memcpy(v5, p, n);
    h1_init(44100);
    memcpy(buf + 1, v5, n);
    CHECK(h1_state_load(buf + 1, n) == 0, "version 5 loads from an odd address");
    p = h1_state(&n);
    CHECK(!memcmp(p, v5, n), "saved and loaded again, to the byte");
}

static void refused(void)
{
    uint32_t n;
    const void *p;
    static uint8_t before[4096];
    h1_init(44100);
    p = h1_state(&n);
    memcpy(before, p, n);
    CHECK(h1_state_load(v4, sizeof(H1State)) != 0, "version 4 at version 5's size is refused");
    memcpy(buf, v5, sizeof(H1State));
    CHECK(h1_state_load(buf, v4_n) != 0, "version 5 at version 4's size is refused");
    buf[offsetof(H1State, version)] = 3;
    CHECK(h1_state_load(buf, sizeof(H1State)) != 0, "version 3 is refused");
    memcpy(buf, v5, sizeof(H1State));
    buf[0] ^= 0x55;
    CHECK(h1_state_load(buf, sizeof(H1State)) != 0, "another magic is refused");
    CHECK(h1_state_load(v5, 4) != 0 && h1_state_load(0, 0) != 0, "a few bytes, or none, are refused");
    p = h1_state(&n);
    CHECK(!memcmp(p, before, n), "a refused state changes nothing");
}

static void new_slots(void)
{
    int i, k, a, kept = 1, fresh = 1;
    memcpy(buf, v5, sizeof(H1State));
    buf[offsetof(H1State, ninst)] = 10;          /* saved before engines 10 and 11 were there */
    buf[offsetof(H1State, nfx)] = 6;             /* and before effects 6 and 7 */
    h1_init(44100);
    CHECK(h1_state_load(buf, sizeof(H1State)) == 0, "a state with fewer engines and effects loads");
    for (i = 0; i < 10; i++)
        kept &= !memcmp(&st.ip[i], v5 + offsetof(H1State, ip) + i * sizeof(InstParams), sizeof(InstParams));
    CHECK(kept, "the engines it was saved with keep their settings");
    for (i = 10; i < 12; i++) {
        uint8_t home[4];
        fresh &= !memcmp(&st.ip[i], &insts[i]->def, sizeof(InstParams));
        for (a = 0; a < NALGO; a++) {
            knobs_home(i, a, home);
            fresh &= !memcmp(st.knob[i][a], home, 4);
        }
    }
    CHECK(fresh, "the engines past them come up as new");
    for (k = 0; k < 4; k++) {
        CHECK(st.fx_p[5][k] == v5[offsetof(H1State, fx_p) + 5 * 4 + k], "an effect it was saved with keeps its knobs");
        CHECK(st.fx_p[6][k] == FX_DEFAULTS[6][k] && st.fx_p[7][k] == FX_DEFAULTS[7][k], "the effects past them come up as new");
    }
    CHECK(st.ninst == NINST && st.nfx == NFX, "and it counts them all now");
}

int main(void)
{
    FILE *f = fopen("tests/state_v4.bin", "rb");
    if (!f) { printf("FAIL: tests/state_v4.bin not found (run from the repository's top)\n"); return 1; }
    v4_n = (uint32_t)fread(v4, 1, sizeof v4, f);
    fclose(f);
    CHECK(v4_n == sizeof(V4) && v4_n == 2648u && v4[offsetof(V4, version)] == 4, "the version 4 fixture");
    version4();
    version5();
    refused();
    new_slots();
    printf("state: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
