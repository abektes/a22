/* A note's place in one key, played in another (KEY FOLLOW), and the scale tables. No engine state: a pure module.
 * A key is one byte: bit 7 valid, bits 4-6 the scale, bits 0-3 the root. */
#include "internal.h"

const uint16_t SCALE_MASK[8] = {
    0xFFF, /* CHROM */
    0xAB5, /* MAJOR */
    0x5AD, /* MINOR */
    0x295, /* PENTA */
    0x4A9, /* MPENT */
    0x4E9, /* BLUES */
    0x6AD, /* DORIAN */
    0x555, /* WHOLE */
};

uint8_t key_byte(int root, int scale)
{
    return (uint8_t)(KEY_VALID | ((scale & 7) << 4) | (((root % 12) + 12) % 12));
}
int key_root(uint8_t k) { return k & 15; }
int key_scale(uint8_t k) { return (k >> 4) & 7; }

static int clamp127(int n) { return n < 0 ? 0 : n > 127 ? 127 : n; }
static int mod12(int n) { return ((n % 12) + 12) % 12; }

/* The scale's notes from its root, up to 12: their count. */
static int scale_notes(int scale, int pc[12])
{
    int i, n = 0;
    for (i = 0; i < 12; i++)
        if (SCALE_MASK[scale] >> i & 1)
            pc[n++] = i;
    return n;
}

/* The note's place: `base` the root's pitch at or below it, the degree (the highest scale note at or below), and
 * the semitones above that degree. */
static void place(int note, int root, int scale, int *base, int *deg, int *acc)
{
    int pc[12], n = scale_notes(scale, pc), rel = mod12(note - root), i = 0;
    while (i + 1 < n && pc[i + 1] <= rel) i++;
    *base = note - rel;
    *deg = i;
    *acc = rel - pc[i];
}

/* Snap down to the nearest note of the key, as scale_snap does for the engine's own key. */
static int snap(int note, int root, int scale)
{
    int i;
    for (i = 0; i < 12; i++)
        if (SCALE_MASK[scale] >> mod12(note - i - root) & 1)
            return note - i;
    return note;
}

static int seven(int scale) { return scale == 1 || scale == 2 || scale == 6; }

int key_map(int note, uint8_t from, uint8_t to)
{
    int r0, r1, s0, s1, d;
    if (!(from & KEY_VALID) || !(to & KEY_VALID) || from == to) return note;
    r0 = key_root(from); s0 = key_scale(from);
    r1 = key_root(to);   s1 = key_scale(to);
    d = mod12(r1 - r0);                      /* the root moves by the smallest interval, -6..+5 */
    if (d > 5) d -= 12;
    if (seven(s0) && seven(s1)) {
        int pc[12], base, deg, acc;
        place(note, r0, s0, &base, &deg, &acc);
        scale_notes(s1, pc);
        return clamp127(base + d + pc[deg] + acc);
    }
    note += d;
    return clamp127(s1 == 0 ? note : snap(note, r1, s1));
}

int key_steps(int note, uint8_t key, int steps)
{
    int pc[12], n, base, deg, acc, idx, oct;
    if (!(key & KEY_VALID) || key_scale(key) == 0) return note;
    n = scale_notes(key_scale(key), pc);
    place(note, key_root(key), key_scale(key), &base, &deg, &acc);
    idx = deg + steps;
    oct = idx >= 0 ? idx / n : -((-idx + n - 1) / n);
    idx -= oct * n;
    return clamp127(base + oct * 12 + pc[idx] + acc);
}
