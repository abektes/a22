/* The saved state, version 6: version 5 (room for 16 engines and 12 effects) with the chord pads a player edited
 * appended. Run by tests/run_tests.sh (from the repository's top, for tests/state_v4.bin and state_v5.bin).
 *
 *   version 4   tests/state_v4.bin (written by 0.4.0's own core: tests/state_v4_gen.c) loads, from any
 *               address, with every table as it was saved; CYCLE and SKINS (engines 13 and 14, after 0.4.0) at
 *               their defaults, the slots no engine or effect has yet empty
 *   version 5   tests/state_v5.bin (written by a version 5 core: tests/state_v5_gen.c) loads, from an odd address,
 *               to the byte but its version, with no pad edited
 *   version 6   edited pads saved and loaded again, to the byte, from an odd address
 *   refused     a wrong size for its version, another version, another magic, a few bytes: nothing changes
 *   pad clamps  a pad record out of range is dropped, the good one beside it kept
 *   new slots   an engine or effect past what a state was saved with (ninst, nfx) comes up as new, the
 *               ones before it as saved: so adding an engine or effect needs no new version. Also a version 5
 *               state from before SKINS (13 engines, its slot empty) brings SKINS up as new, CYCLE as saved;
 *               and one saved with nine effects (GRAIN, in development builds only) and the ninth in hand
 *               comes up on PLATE, the eight it keeps as saved
 *   project     a project's state and loop in place of what plays (h1_project_load), of version 5 or 6: taken whole
 *               but TUNE and BRIGHT, which stay the unit's; both count as changed (the loop dirty); a refused state
 *               changes nothing */
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

static uint8_t v4[4096], v5[4096], buf[8192], cur[4096];   /* cur: a state as this core saves it */
static uint32_t v4_n, v5_n;

static int zero(const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--) if (*b++) return 0;
    return 1;
}
#define SAME(field, off, n) (!memcmp((const uint8_t *)&st + offsetof(H1State, field), v4 + (off), (n)))

static int engines_new_from(int first)            /* every engine from first on at its own defaults */
{
    int i, a, ok = 1;
    for (i = first; i < NINST; i++) {
        uint8_t home[4];
        ok &= !memcmp(&st.ip[i], &insts[i]->def, sizeof(InstParams));
        for (a = 0; a < NALGO; a++) {
            knobs_home(i, a, home);
            ok &= !memcmp(st.knob[i][a], home, 4);
        }
    }
    return ok;
}

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
        CHECK(SAME(master, offsetof(V4, master), offsetof(V4, ninst) - offsetof(V4, master)),
              "master, shake, riff and chord memory, as saved");
        CHECK(v4[offsetof(V4, ninst)] == 12 && st.ninst == NINST, "saved with twelve engines, counted as all of them now");
        CHECK(SAME(ip, offsetof(V4, ip), sizeof ((V4 *)0)->ip), "the twelve engines' settings, as saved");
        CHECK(SAME(knob, offsetof(V4, knob), sizeof ((V4 *)0)->knob), "every algorithm's knobs, as saved");
        CHECK(SAME(trk, offsetof(V4, trk), sizeof(V4) - offsetof(V4, trk)),
              "the tracks, snapshots and steps, as saved");
        CHECK(zero(st.fx_p[8], sizeof st.fx_p - sizeof st.fx_p[0] * 8), "the new effect slots are empty");
        CHECK(zero(&st.ip[NINST], sizeof st.ip[0] * (NSLOT - NINST)) &&
              zero(st.knob[NINST], sizeof st.knob[0] * (NSLOT - NINST)), "the slots no engine has yet are empty");
        CHECK(engines_new_from(12), "the engines version 4 did not have (CYCLE, SKINS) come up as new");
        CHECK(st.nfx == NFX && st.version == H1_VERSION, "it is the current version now, with its effects counted");
        CHECK(st.pad_mask == 0 && zero(st.pad, sizeof st.pad) && zero(st.spare, sizeof st.spare), "no pad edited");
        CHECK(st.rsv[0] == 0 && st.rsv[1] == 0 && st.trk[0].rsv == 0 && st.trk[1].rsv == 0 &&
              st.trk[2].rsv == 0 && st.trk[3].rsv == 0, "no step key, and no track follows a key");
    }
    CHECK(h1_instrument() == 10 && h1_algorithm() == 10 % 8, "the engine and algorithm in hand, as saved");
}

static void version5(void)
{
    uint32_t n;
    const uint8_t *p;
    h1_init(44100);
    memcpy(buf + 1, v5, v5_n);
    CHECK(h1_state_load(buf + 1, v5_n) == 0, "a version 5 state loads, from an odd address");
    CHECK(st.version == 6 && st.pad_mask == 0 && zero(st.pad, sizeof st.pad) && zero(st.spare, sizeof st.spare),
          "version 6 now, no pad edited");
    CHECK(st.chord[0] == CHORD_PADS && st.rsv[1] == 10 && st.chord[2] == VOICE_SMOOTH && st.chord[3] == 20,
          "the chord pads as saved");
    CHECK(st.keys[0] == 5 && st.keys[1] == 3 && st.keys[2] == 40 && st.keys[3] == 1 && st.glo[0] == 133 &&
          st.octave == -1 && st.rsv[0] == 0x85, "the keys, TEMPO, octave and the step key");
    CHECK(st.chord_n == 3 && st.chord_mem[1] == 4 && st.chord_mem[2] == 7, "MEMORY's chord");
    CHECK(st.trk[2].vol == 61 && st.step_vel[NSTEP - 1][NSNOTE - 1] == 99, "a track, and the last step");
    p = (const uint8_t *)h1_state(&n);
    CHECK(!memcmp(p, v5, offsetof(H1State, version)) &&
          !memcmp(p + offsetof(H1State, version) + 1, v5 + offsetof(H1State, version) + 1, v5_n - offsetof(H1State, version) - 1),
          "every byte of it, but the version");
}

static const ChUser ONE = {1, CH_MEM, 0, 3, {0, 6, 13, 0, 0, 0}}, TWO = {7, 1, 2, 3, {0, 3, 7, 0, 0, 0}};

static void version6(void)
{
    uint32_t n;
    const void *p;
    h1_init(44100);
    h1_state_load(v4, v4_n);
    st.pad[0] = ONE; st.pad[7] = TWO; st.pad_mask = 0x81;
    p = h1_state(&n);
    CHECK(n == sizeof(H1State) && n == 2984u, "version 6 is 2984 bytes");
    memcpy(cur, p, n);
    h1_init(44100);
    memcpy(buf + 1, cur, n);
    CHECK(h1_state_load(buf + 1, n) == 0, "version 6 loads from an odd address");
    p = h1_state(&n);
    CHECK(!memcmp(p, cur, n) && st.pad_mask == 0x81, "saved and loaded again, to the byte");
}

static void refused(void)
{
    uint32_t n;
    const void *p;
    static uint8_t before[4096];
    h1_init(44100);
    p = h1_state(&n);
    memcpy(before, p, n);
    CHECK(h1_state_load(v4, sizeof(H1State)) != 0, "version 4 at version 6's size is refused");
    memcpy(buf, cur, sizeof(H1State));
    CHECK(h1_state_load(buf, v4_n) != 0, "version 6 at version 4's size is refused");
    CHECK(h1_state_load(buf, v5_n) != 0, "version 6 at version 5's size is refused");
    memset(buf, 0, sizeof(H1State));
    memcpy(buf, v5, v5_n);
    CHECK(h1_state_load(buf, sizeof(H1State)) != 0, "version 5 at version 6's size is refused");
    memcpy(buf, cur, sizeof(H1State));
    buf[offsetof(H1State, version)] = 3;
    CHECK(h1_state_load(buf, sizeof(H1State)) != 0, "version 3 is refused");
    memcpy(buf, cur, sizeof(H1State));
    buf[0] ^= 0x55;
    CHECK(h1_state_load(buf, sizeof(H1State)) != 0, "another magic is refused");
    CHECK(h1_state_load(cur, 4) != 0 && h1_state_load(0, 0) != 0, "a few bytes, or none, are refused");
    p = h1_state(&n);
    CHECK(!memcmp(p, before, n), "a refused state changes nothing");
}

static void pad_clamps(void)
{
    static const struct { int at, v; const char *what; } BAD[] = {
        {offsetof(ChUser, off), 12, "a root past B"},      {offsetof(ChUser, type), CH_MEM + 1, "no such type"},
        {offsetof(ChUser, n), 0, "no notes"},              {offsetof(ChUser, n), CHORD_MAX + 1, "too many notes"},
        {offsetof(ChUser, inv), 3, "more inversion than notes"}, {offsetof(ChUser, iv), 100, "a note too far"},
        {offsetof(ChUser, iv) + 1, 0x80, "a note too far down"},
    };
    int i;
    for (i = 0; i < (int)(sizeof BAD / sizeof BAD[0]); i++) {
        memcpy(buf, cur, sizeof(H1State));
        memcpy(buf + offsetof(H1State, pad) + sizeof(ChUser), &TWO, sizeof(ChUser));
        buf[offsetof(H1State, pad_mask)] = 0x03;
        buf[offsetof(H1State, pad) + BAD[i].at] = (uint8_t)BAD[i].v;
        h1_init(44100);
        CHECK(h1_state_load(buf, sizeof(H1State)) == 0, BAD[i].what);
        CHECK(st.pad_mask == 0x02 && zero(&st.pad[0], sizeof st.pad[0]) && !memcmp(&st.pad[1], &TWO, sizeof TWO), BAD[i].what);
    }
}

static void new_slots(void)
{
    int i, k, kept = 1, fresh;
    memcpy(buf, cur, sizeof(H1State));
    buf[offsetof(H1State, ninst)] = 10;          /* saved before engines 10 and 11 were there */
    buf[offsetof(H1State, nfx)] = 6;             /* and before effects 6 and 7 */
    h1_init(44100);
    CHECK(h1_state_load(buf, sizeof(H1State)) == 0, "a state with fewer engines and effects loads");
    for (i = 0; i < 10; i++)
        kept &= !memcmp(&st.ip[i], cur + offsetof(H1State, ip) + i * sizeof(InstParams), sizeof(InstParams));
    CHECK(kept, "the engines it was saved with keep their settings");
    fresh = engines_new_from(10);
    CHECK(fresh, "the engines past them come up as new");
    for (k = 0; k < 4; k++) {
        CHECK(st.fx_p[5][k] == cur[offsetof(H1State, fx_p) + 5 * 4 + k], "an effect it was saved with keeps its knobs");
        CHECK(st.fx_p[6][k] == FX_DEFAULTS[6][k] && st.fx_p[7][k] == FX_DEFAULTS[7][k], "the effects past them come up as new");
    }
    CHECK(st.ninst == NINST && st.nfx == NFX, "and it counts them all now");

    /* version 5 as a 13-engine build saved it: SKINS' slot empty (all zeros), CYCLE's moved off its defaults */
    memcpy(buf, v5, v5_n);
    buf[offsetof(H1State, ninst)] = 13;
    memset(buf + offsetof(H1State, ip) + 13 * sizeof(InstParams), 0, sizeof(InstParams));
    memset(buf + offsetof(H1State, knob) + 13 * NALGO * 4, 0, NALGO * 4);
    buf[offsetof(H1State, ip) + 12 * sizeof(InstParams)] ^= 0x11;
    h1_init(44100);
    CHECK(h1_state_load(buf, v5_n) == 0, "a version 5 state from before SKINS loads");
    CHECK(engines_new_from(13), "SKINS comes up as new, not with the empty slot's zeros");
    CHECK(!memcmp(&st.ip[12], buf + offsetof(H1State, ip) + 12 * sizeof(InstParams), sizeof(InstParams)),
          "CYCLE keeps what was saved");

    /* version 5 as a development build with a ninth effect (GRAIN, taken out) saved it, with that effect in hand */
    memcpy(buf, v5, v5_n);
    buf[offsetof(H1State, nfx)] = 9;
    buf[offsetof(H1State, fx_type)] = 8;
    memset(buf + offsetof(H1State, fx_p) + 8 * 4, 50, 4);
    h1_init(44100);
    CHECK(h1_state_load(buf, v5_n) == 0, "a state saved with nine effects loads");
    CHECK(st.fx_type == FX_PLATE && st.nfx == NFX, "the effect it had in hand, gone now, is PLATE");
    for (i = NFX, kept = 1; i < NFXSLOT; i++)
        for (k = 0; k < 4; k++)
            kept &= st.fx_p[i][k] == 0;
    CHECK(kept, "the slots past the effects there are now are empty");
    CHECK(!memcmp(st.fx_p, v5 + offsetof(H1State, fx_p), NFX * 4), "the effects there are keep their knobs");
}

static void project(void)
{
    uint32_t n, ln;
    const void *p;
    static uint8_t proj_state[4096], proj_loop[64];
    uint8_t *q;
    h1_init(44100);
    memcpy(proj_state, v5, v5_n);
    memset(proj_state + v5_n, 0, sizeof(H1State) - v5_n);
    proj_state[offsetof(H1State, glo) + 0] = 150;    /* the project's TEMPO, TUNE and BRIGHT */
    proj_state[offsetof(H1State, glo) + 2] = 80;
    proj_state[offsetof(H1State, glo) + 3] = 30;
    (void)h1_loop(&ln);                          /* an empty loop's blob, as a project holds one */
    memcpy(proj_loop, h1_loop(&ln), ln);
    st.glo[2] = 61;                              /* the unit's now */
    st.glo[3] = 77;
    h1_loop_saved();
    CHECK(h1_project_load(proj_state, v5_n, proj_loop, ln) == 0, "a project saved as version 5 loads");
    p = h1_state(&n);
    q = (uint8_t *)p;
    CHECK(q[offsetof(H1State, glo) + 0] == 150, "its TEMPO is taken");
    CHECK(q[offsetof(H1State, glo) + 2] == 61 && q[offsetof(H1State, glo) + 3] == 77, "TUNE and BRIGHT stay the unit's");
    proj_state[offsetof(H1State, glo) + 2] = 61;
    proj_state[offsetof(H1State, glo) + 3] = 77;
    proj_state[offsetof(H1State, version)] = 6;
    CHECK(!memcmp(p, proj_state, n), "the rest of its state is taken to the byte");
    CHECK(h1_loop_dirty(), "the loop counts as changed, so the ring saves it");
    memcpy(proj_state, p, n);
    proj_state[0] ^= 0x55;
    CHECK(h1_project_load(proj_state, sizeof(H1State), proj_loop, ln) != 0, "a project whose state is refused is refused");
    CHECK(q[offsetof(H1State, glo) + 0] == 150 && st.magic == H1_MAGIC, "and changes nothing");
    memcpy(proj_state, cur, sizeof(H1State));
    proj_state[offsetof(H1State, glo) + 2] = 61;
    proj_state[offsetof(H1State, glo) + 3] = 77;
    CHECK(h1_project_load(proj_state, sizeof(H1State), proj_loop, ln) == 0, "a project with edited pads loads");
    CHECK(st.pad_mask == 0x81 && !memcmp(h1_state(&n), proj_state, sizeof(H1State)), "its pads come back, to the byte");
}

int main(void)
{
    FILE *f = fopen("tests/state_v4.bin", "rb");
    if (!f) { printf("FAIL: tests/state_v4.bin not found (run from the repository's top)\n"); return 1; }
    v4_n = (uint32_t)fread(v4, 1, sizeof v4, f);
    fclose(f);
    CHECK(v4_n == sizeof(V4) && v4_n == 2648u && v4[offsetof(V4, version)] == 4, "the version 4 fixture");
    f = fopen("tests/state_v5.bin", "rb");
    if (!f) { printf("FAIL: tests/state_v5.bin not found (run from the repository's top)\n"); return 1; }
    v5_n = (uint32_t)fread(v5, 1, sizeof v5, f);
    fclose(f);
    CHECK(v5_n == 2888u && v5[offsetof(H1State, version)] == 5, "the version 5 fixture");
    version4();
    version5();
    version6();
    refused();
    pad_clamps();
    new_slots();
    project();
    printf("state: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
