/* The chord pads' types and sets, and what is worked out from them: a pad as a record, its notes, name and numeral,
 * a record captured from notes held, and SMOOTH's voicing. No engine state: a pure module. The sets are A22's own,
 * three a mood. */
#include "internal.h"

/* the order is the sets' (a pad names its type by number) */
enum { MAJ, MIN, DIM, AUG, SUS2, SUS4, DOM7, MAJ7, MIN7, MIN7B5, DIM7, SIX, MIN6, ADD9, MINADD9, MAJ9, MIN9, DOM9,
       SUS7, MIN11 };

const ChType CH_TYPES[NCHTYPE] = {
    {"MAJ",     {0, 4, 7, -1, -1}},   {"MIN",     {0, 3, 7, -1, -1}},
    {"DIM",     {0, 3, 6, -1, -1}},   {"AUG",     {0, 4, 8, -1, -1}},
    {"SUS2",    {0, 2, 7, -1, -1}},   {"SUS4",    {0, 5, 7, -1, -1}},
    {"7",       {0, 4, 7, 10, -1}},   {"MAJ7",    {0, 4, 7, 11, -1}},
    {"MIN7",    {0, 3, 7, 10, -1}},   {"MIN7-5",  {0, 3, 6, 10, -1}},
    {"DIM7",    {0, 3, 6, 9, -1}},    {"6",       {0, 4, 7, 9, -1}},
    {"MIN6",    {0, 3, 7, 9, -1}},    {"ADD9",    {0, 4, 7, 14, -1}},
    {"MINADD9", {0, 3, 7, 14, -1}},   {"MAJ9",    {0, 4, 7, 11, 14}},
    {"MIN9",    {0, 3, 7, 10, 14}},   {"9",       {0, 4, 7, 10, 14}},
    {"7SUS4",   {0, 5, 7, 10, -1}},   {"MIN11",   {0, 3, 7, 10, 17}},
};

const char *const CH_TYPE_NAMES[NCHTYPE + 1] = {
    CH_TYPES[0].name,  CH_TYPES[1].name,  CH_TYPES[2].name,  CH_TYPES[3].name,  CH_TYPES[4].name,
    CH_TYPES[5].name,  CH_TYPES[6].name,  CH_TYPES[7].name,  CH_TYPES[8].name,  CH_TYPES[9].name,
    CH_TYPES[10].name, CH_TYPES[11].name, CH_TYPES[12].name, CH_TYPES[13].name, CH_TYPES[14].name,
    CH_TYPES[15].name, CH_TYPES[16].name, CH_TYPES[17].name, CH_TYPES[18].name, CH_TYPES[19].name,
    "MEM",
};

const char *const CH_SET_NAMES[NCHSET] = {
    "POP 1", "POP 2", "POP 3",             "BALLAD 1", "BALLAD 2", "BALLAD 3",
    "SOUL 1", "SOUL 2", "SOUL 3",          "LOFI 1", "LOFI 2", "LOFI 3",
    "JAZZ 1", "JAZZ 2", "JAZZ 3",          "CINEMA 1", "CINEMA 2", "CINEMA 3",
    "HOUSE 1", "HOUSE 2", "HOUSE 3",       "DARK 1", "DARK 2", "DARK 3",
};

/* each: minor?, then eight pads as {semitones above the key's root, type} */
const ChSet CH_SETS[NCHSET] = {
    {0, {{0, MAJ}, {7, MAJ}, {9, MIN}, {5, MAJ}, {2, MIN}, {4, MIN}, {5, ADD9}, {7, SUS4}}},              /* POP 1 */
    {0, {{0, ADD9}, {4, MIN7}, {9, MIN7}, {5, ADD9}, {2, MIN7}, {7, SUS4}, {7, MAJ}, {10, MAJ}}},         /* POP 2 */
    {1, {{0, MIN}, {8, MAJ}, {3, MAJ}, {10, MAJ}, {5, MIN}, {7, MIN}, {8, ADD9}, {10, SUS4}}},            /* POP 3 */
    {0, {{0, MAJ}, {4, MIN}, {5, MAJ7}, {7, SUS4}, {9, MIN7}, {2, MIN7}, {5, MIN6}, {7, MAJ}}},           /* BALLAD 1 */
    {0, {{0, ADD9}, {9, MINADD9}, {5, MAJ9}, {7, SUS7}, {4, MIN7}, {4, DOM7}, {2, MIN9}, {5, SIX}}},      /* BALLAD 2 */
    {1, {{0, MINADD9}, {3, MAJ7}, {8, MAJ7}, {10, SUS4}, {5, MIN7}, {7, SUS7}, {7, DOM7}, {8, ADD9}}},    /* BALLAD 3 */
    {0, {{0, MAJ7}, {4, MIN7}, {9, MIN7}, {2, MIN9}, {5, MAJ7}, {7, DOM9}, {4, DOM7}, {8, MAJ7}}},        /* SOUL 1 */
    {0, {{0, SIX}, {9, DOM7}, {2, MIN7}, {7, DOM9}, {5, MAJ9}, {6, DIM7}, {4, MIN7}, {5, MIN6}}},         /* SOUL 2 */
    {1, {{0, MIN7}, {5, DOM9}, {8, MAJ7}, {7, DOM7}, {3, MAJ9}, {10, SIX}, {2, MIN7B5}, {5, MIN9}}},      /* SOUL 3 */
    {0, {{0, MAJ9}, {2, MIN9}, {4, MIN7}, {5, MAJ9}, {9, MIN9}, {7, SUS7}, {9, DOM7}, {5, MIN6}}},        /* LOFI 1 */
    {1, {{0, MIN9}, {5, MIN7}, {8, MAJ7}, {10, DOM7}, {3, MAJ7}, {2, MIN7B5}, {7, DOM7}, {1, MAJ7}}},     /* LOFI 2 */
    {1, {{0, MIN11}, {10, SIX}, {8, MAJ9}, {7, MIN7}, {5, MIN9}, {3, MAJ9}, {2, MIN7}, {1, MAJ9}}},       /* LOFI 3 */
    {0, {{2, MIN7}, {7, DOM9}, {0, MAJ9}, {9, DOM7}, {5, MAJ7}, {11, MIN7B5}, {4, DOM7}, {9, MIN7}}},     /* JAZZ 1 */
    {0, {{0, MAJ7}, {3, DIM7}, {2, MIN9}, {1, DOM7}, {4, MIN7}, {10, DOM9}, {5, MIN6}, {8, MAJ7}}},       /* JAZZ 2 */
    {1, {{2, MIN7B5}, {7, DOM7}, {0, MIN6}, {5, MIN7}, {10, DOM9}, {3, MAJ7}, {8, MAJ7}, {11, DIM7}}},    /* JAZZ 3 */
    {1, {{0, MIN}, {8, MAJ}, {5, MIN}, {7, MAJ}, {3, MAJ}, {10, MAJ}, {1, MAJ}, {0, SUS2}}},              /* CINEMA 1 */
    {0, {{0, SUS2}, {8, MAJ}, {10, MAJ}, {0, MAJ}, {5, MIN}, {3, MAJ}, {7, SUS4}, {2, MAJ}}},             /* CINEMA 2 */
    {1, {{0, MINADD9}, {4, MAJ}, {8, MAJ7}, {3, AUG}, {5, MINADD9}, {1, MAJ7}, {6, MIN}, {7, SUS4}}},     /* CINEMA 3 */
    {1, {{0, MIN7}, {3, MAJ7}, {5, MIN7}, {7, MIN7}, {8, MAJ7}, {10, SIX}, {0, MIN9}, {5, DOM9}}},        /* HOUSE 1 */
    {1, {{0, MIN9}, {10, MIN9}, {8, MAJ9}, {7, MIN9}, {5, MIN9}, {3, MAJ9}, {2, MIN7}, {10, SUS7}}},      /* HOUSE 2 */
    {0, {{0, MAJ7}, {2, MIN7}, {4, MIN7}, {5, MAJ7}, {7, SUS7}, {9, MIN7}, {5, SIX}, {10, MAJ7}}},        /* HOUSE 3 */
    {1, {{0, MIN}, {1, MAJ}, {5, MIN}, {7, DIM}, {8, MAJ}, {3, MIN}, {6, MAJ}, {0, SUS2}}},               /* DARK 1 */
    {1, {{0, MINADD9}, {6, DIM}, {8, MIN}, {1, MAJ7}, {5, MIN6}, {7, DOM7}, {11, DIM7}, {3, AUG}}},       /* DARK 2 */
    {1, {{0, MIN7}, {10, MIN7}, {8, MAJ7}, {1, MAJ7}, {5, MIN11}, {6, MAJ7}, {3, MIN}, {7, SUS7}}},       /* DARK 3 */
};

static const ChSet *set_of(int set) { return &CH_SETS[set >= 0 && set < NCHSET ? set : 0]; }

int ch_pad_pc(int set, int pad, int root)
{
    return (((root + set_of(set)->pad[pad & 7].off) % 12) + 12) % 12;
}

const char *ch_type_name(int type) { return CH_TYPE_NAMES[type >= 0 && type < NCHTYPE ? type : NCHTYPE]; }

void ch_set_type(ChUser *u, int type)
{
    const ChType *t = &CH_TYPES[type >= 0 && type < NCHTYPE ? type : 0];
    int i, n = 0;
    for (i = 0; i < CHORD_MAX; i++) {
        int has = i < 5 && t->iv[i] >= 0;
        u->iv[i] = (int8_t)(has ? t->iv[i] : 0);
        n += has;
    }
    u->type = (uint8_t)(t - CH_TYPES);
    u->n = (uint8_t)n;
    if (u->inv >= n) u->inv = (uint8_t)(n - 1);
}

void ch_pad_default(int set, int pad, ChUser *out)
{
    ChPad p = set_of(set)->pad[pad & 7];
    out->off = p.off;
    out->inv = 0;
    ch_set_type(out, p.type);
}

int ch_base(int off, int root, int octave) { return 48 + (((root + off) % 12) + 12) % 12 + 12 * octave; }

static void ascending(int *nt, int n)
{
    int i, j;
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && nt[j - 1] > nt[j]; j--) { int t = nt[j]; nt[j] = nt[j - 1]; nt[j - 1] = t; }
}

SMALL int ch_user_notes(const ChUser *u, int root, int octave, int nt[CHORD_MAX])
{
    int i, n = u->n <= CHORD_MAX ? u->n : CHORD_MAX, base = ch_base(u->off, root, octave);
    for (i = 0; i < n; i++) nt[i] = base + u->iv[i];
    ascending(nt, n);
    for (i = 0; i < n && i < u->inv; i++) nt[i] += 12;
    ascending(nt, n);
    return n;
}

SMALL int ch_pad_notes(int set, int pad, int root, int octave, int nt[5])
{
    ChUser u;
    int all[CHORD_MAX], i, n;
    ch_pad_default(set, pad, &u);                            /* (a type has five notes at most) */
    n = ch_user_notes(&u, root, octave, all);
    for (i = 0; i < n && i < 5; i++) nt[i] = all[i];
    return i;
}

int ch_numeral(int minor, const ChUser *u, char out[9])
{
    static const char *const NUM[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    static const uint8_t DEG[2][7] = {{0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}};   /* major, natural minor */
    const char *src = "BORROWED";
    int d, i, third = 0, kind = CH_NUM_BORROWED;
    for (i = 0; i < u->n && i < CHORD_MAX; i++) {            /* (bit 0: a minor third above the root, bit 1: a major) */
        int pc = ((u->iv[i] % 12) + 12) % 12;
        third |= pc == 3 ? 1 : pc == 4 ? 2 : 0;
    }
    for (d = 0; d < 7; d++)
        if (DEG[minor & 1][d] == u->off) {
            src = NUM[d];
            kind = third == 1 ? CH_NUM_MINOR : CH_NUM_MAJOR;
        }
    for (i = 0; i < 8 && src[i]; i++)
        out[i] = src[i];
    out[i] = 0;
    return kind;
}

int ch_pad_numeral(int set, int pad, char out[9])
{
    ChUser u;
    ch_pad_default(set, pad, &u);
    return ch_numeral(set_of(set)->minor, &u, out);
}

SMALL void ch_capture(const uint8_t *note, int n, int root, int octave, ChUser *out)
{
    int i, k, t, have = 0, base, r, type = CH_MEM;
    if (n > CHORD_MAX) n = CHORD_MAX;
    r = note[0] % 12;
    for (i = 0; i < n; i++) have |= 1 << (note[i] % 12);
    for (k = 0; k < n && type == CH_MEM; k++)
        for (t = 0; t < NCHTYPE && type == CH_MEM; t++) {
            int want = 0;
            for (i = 0; i < 5 && CH_TYPES[t].iv[i] >= 0; i++)
                want |= 1 << ((note[k] + CH_TYPES[t].iv[i]) % 12);
            if (want == have) {
                type = t;
                r = note[k] % 12;
            }
        }
    out->off = (uint8_t)((((r - root) % 12) + 12) % 12);
    out->type = (uint8_t)type;
    out->inv = 0;
    out->n = (uint8_t)n;
    base = ch_base(out->off, root, octave);
    for (i = 0; i < CHORD_MAX; i++) {
        int iv = i < n ? note[i] - base : 0;
        out->iv[i] = (int8_t)(iv < -48 ? -48 : iv > 48 ? 48 : iv);
    }
}

SMALL int ch_centre(const int *nt, int n, int octave)
{
    int i, sum = 0;
    for (i = 0; i < n; i++) sum += nt[i];
    return n > 0 ? sum * 16 / n - 192 * octave : -1;
}

/* SMOOTH: of the chord's inversions, where it is and an octave down, the first whose centre is nearest the last
 * chord's. That keeps it within an octave of root position, so a long run of chords never drifts away; the centres
 * leave the octave out, so OCT still moves the next chord. */
SMALL int ch_smooth(int *nt, int n, int last, int octave)
{
    int c[CHORD_MAX], i, k, shift, best = -1, best_k = 0, best_shift = 0;
    if (n > CHORD_MAX) n = CHORD_MAX;
    if (last < 0 || n < 1) return ch_centre(nt, n, octave);
    for (shift = 0; shift >= -12; shift -= 12)
        for (k = 0; k < n; k++) {                            /* (inversion k: the lowest k an octave up) */
            int d;
            for (i = 0; i < n; i++) c[i] = nt[i] + shift + (i < k ? 12 : 0);
            d = ch_centre(c, n, octave) - last;
            if (d < 0) d = -d;
            if (best < 0 || d < best) {
                best = d;
                best_k = k;
                best_shift = shift;
            }
        }
    for (i = 0; i < n; i++) nt[i] += best_shift + (i < best_k ? 12 : 0);
    ascending(nt, n);
    return ch_centre(nt, n, octave);
}
