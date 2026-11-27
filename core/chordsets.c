/* The chord pads' types and sets, and what is worked out from them: a pad's notes, its numeral, and SMOOTH's
 * voicing. No engine state: a pure module. The sets are A22's own, three a mood. */
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

SMALL int ch_pad_notes(int set, int pad, int root, int octave, int nt[5])
{
    const ChType *t = &CH_TYPES[set_of(set)->pad[pad & 7].type];
    int i, base = 48 + ch_pad_pc(set, pad, root) + 12 * octave;   /* (the root in the octave from C3) */
    for (i = 0; i < 5 && t->iv[i] >= 0; i++)
        nt[i] = base + t->iv[i];
    return i;
}

int ch_pad_numeral(int set, int pad, char out[9])
{
    static const char *const NUM[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    static const uint8_t DEG[2][7] = {{0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}};   /* major, natural minor */
    const ChSet *s = set_of(set);
    ChPad p = s->pad[pad & 7];
    const char *src = "BORROWED";
    int d, i, kind = CH_NUM_BORROWED;
    for (d = 0; d < 7; d++)
        if (DEG[s->minor & 1][d] == p.off) {
            src = NUM[d];
            kind = CH_TYPES[p.type].iv[1] == 3 ? CH_NUM_MINOR : CH_NUM_MAJOR;
        }
    for (i = 0; i < 8 && src[i]; i++)
        out[i] = src[i];
    out[i] = 0;
    return kind;
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
    int c[CHORD_MAX], i, j, k, shift, best = -1, best_k = 0, best_shift = 0;
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
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && nt[j - 1] > nt[j]; j--) { int t = nt[j]; nt[j] = nt[j - 1]; nt[j - 1] = t; }
    return ch_centre(nt, n, octave);
}
