/* h1 - the public calls that are not the UI's or the engine's own: start-up and saved state. */
#include "internal.h"

void knobs_default(int inst, int algo)
{
    int a, k;
    for (a = 0; a < NALGO; a++)
        if (algo < 0 || a == algo)
            for (k = 0; k < 4; k++)
                st.knob[inst][a][k] = insts[inst]->def.knob0[k];
}

static void defaults(void)
{
    int i;
    uint8_t *p = (uint8_t *)&st;
    for (i = 0; i < (int)sizeof st; i++)
        p[i] = 0;
    st.magic = H1_MAGIC;
    st.version = H1_VERSION;
    st.glo[0] = 112;                             /* tempo */
    st.glo[2] = 50;                              /* tune: centre */
    st.glo[3] = 100;                             /* bright */
    st.seq[1] = 1;                               /* 2 bars */
    st.seq[3] = 1;                               /* the loop snaps to 1/16 */
    st.seq[4] = 1;                               /* steps of 1/16 */
    for (i = 0; i < NINST; i++) {
        st.ip[i] = insts[i]->def;
        knobs_default(i, -1);
        st.ink[i][0] = insts[i]->ink[0];
        st.ink[i][1] = insts[i]->ink[1];
        st.ink[i][2] = insts[i]->ink[2];
    }
}

void h1_init(uint32_t sample_rate)
{
    defaults();
    eng_init(sample_rate);
    ui_init();
}

void h1_frame(uint32_t now_ms) { ui_frame(now_ms); }
void h1_render(int32_t *lr, uint32_t n) { eng_render(lr, n); }

const void *h1_state(uint32_t *size)
{
    if (size)
        *size = (uint32_t)sizeof st;
    return &st;
}

/* Take a saved blob. Call it right after h1_init, before the audio starts. Anything that does not
 * look like a state of this version or of the two before it is refused, and every value is clamped to
 * its range.
 *
 * The blob is bytes at any address: it is copied out a byte at a time and nothing is read from it as
 * a word. (A byte buffer may sit at an odd address, and the FM-1's CPU stops on a word read from one:
 * reading the magic straight out of the blob kept the first h1 from starting once it had saved.) */
typedef char v4_is_664_bytes[H1_VERSION_4_SIZE == 664u ? 1 : -1];   /* what the FM-1s out there hold: these */
typedef char v5_is_920_bytes[H1_VERSION_5_SIZE == 920u ? 1 : -1];   /* must not move with the layout */
int h1_state_load(const void *blob, uint32_t size)
{
    const uint8_t *b = (const uint8_t *)blob;
    uint8_t *d = (uint8_t *)&st;
    const uint32_t body = (uint32_t)offsetof(H1State, steps);   /* where versions 4, 5 and 6 part ways */
    uint32_t magic = 0, was, n;
    int i, j, pg, a;
    if (!blob)
        return -1;
    was = size == sizeof st ? H1_VERSION : size == H1_VERSION_5_SIZE ? 5u : size == H1_VERSION_4_SIZE ? 4u : 0u;
    if (!was)
        return -1;
    for (n = 0; n < 4u; n++)
        ((uint8_t *)&magic)[n] = b[n];
    if (magic != H1_MAGIC || b[offsetof(H1State, version)] != was)
        return -1;                               /* (refused: nothing has been touched) */
    if (was == H1_VERSION)
        for (n = 0; n < sizeof st; n++)
            d[n] = b[n];
    else {
        /* An older state: the same up to the steps. A step held four notes there, with in version 5 the
         * part of each after all the notes; LAYERS came with version 5; and an instrument's algorithms
         * shared four knobs, so every algorithm starts from where those stood and nothing sounds
         * different after the update. */
        const uint8_t *notes = b + body, *parts = was >= 5u ? notes + NSTEP * H1_OLD_SNOTE : 0;
        for (n = 0; n < body; n++)
            d[n] = b[n];
        for (i = 0; i < NSTEP; i++)
            for (j = 0; j < NSNOTE; j++) {
                st.steps[i][j] = j < (int)H1_OLD_SNOTE ? notes[(uint32_t)i * H1_OLD_SNOTE + (uint32_t)j] : 0;
                st.step_part[i][j] = parts && j < (int)H1_OLD_SNOTE ? parts[(uint32_t)i * H1_OLD_SNOTE + (uint32_t)j] : 0;
            }
        if (was < 5u)
            st.seq[6] = 0;
        for (i = 0; i < NINST; i++)
            for (a = 0; a < NALGO; a++)
                for (j = 0; j < 4; j++)
                    st.knob[i][a][j] = st.ip[i].knob0[j];
        st.version = H1_VERSION;
    }
    if (st.inst >= NINST) st.inst = 0;
    st.octave = (int8_t)iclamp(st.octave, -2, 2);
    st.hud &= 1;
    for (i = 0; i < NINST; i++) {
        uint8_t keep = st.inst;
        st.inst = (uint8_t)i;
        for (j = 0; j < 4; j++) {
            if (st.ip[i].knob0[j] > 100) st.ip[i].knob0[j] = 100;
            for (a = 0; a < NALGO; a++)
                if (st.knob[i][a][j] > 100) st.knob[i][a][j] = 100;
        }
        if (st.ip[i].algo >= NALGO) st.ip[i].algo = 0;
        for (pg = 1; pg < NPAGE; pg++)
            for (j = 0; j < 4; j++) {
                uint8_t *v = page_value(pg, j);
                *v = (uint8_t)iclamp(*v, page_param(pg, j)->min, page_param(pg, j)->max);
            }
        st.inst = keep;
    }
    st.seq[0] &= 1;
    if (st.seq[3] > 2) st.seq[3] = 1;
    if (st.seq[4] > 2) st.seq[4] = 1;
    if (st.seq[5] > 2) st.seq[5] = CLICK_REC;
    st.seq[6] &= 1;
    for (i = 0; i < NSTEP; i++)
        for (j = 0; j < NSNOTE; j++)
            if (st.steps[i][j] > 128) st.steps[i][j] = 0;
    eng_init(0);
    ui_init();
    return 0;
}

int h1_instrument(void) { return st.inst; }
const char *h1_instrument_name(int i) { return i >= 0 && i < NINST ? insts[i]->name : ""; }
int h1_algorithm(void) { return st.ip[st.inst].algo; }
int h1_page(void) { return ui_page(); }
int h1_layers(void) { return st.seq[6] == 1; }
void h1_two_cores(int (*run)(void (*fn)(uint32_t), uint32_t arg), int (*wait)(void)) { eng_two_cores(run, wait); }
int h1_instrument_count(void) { return NINST; }
const char *h1_knob_name(int inst, int k) { return inst >= 0 && inst < NINST && k >= 0 && k < 4 ? insts[inst]->knob[k] : ""; }
const char *h1_algo_name(int inst, int a) { return inst >= 0 && inst < NINST && a >= 0 && a < NALGO ? insts[inst]->algo[a] : ""; }
uint32_t h1_voices_active(void) { return eng_voices(); }

void h1_ink(int inst, uint8_t rgb[3])
{
    int i;
    for (i = 0; i < 3; i++)
        rgb[i] = inst >= 0 && inst < NINST ? st.ink[inst][i] : 255;
}

void h1_set_ink(int inst, uint8_t r, uint8_t g, uint8_t b)
{
    if (inst < 0 || inst >= NINST)
        return;
    st.ink[inst][0] = r;
    st.ink[inst][1] = g;
    st.ink[inst][2] = b;
}
