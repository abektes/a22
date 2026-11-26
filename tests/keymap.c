/* The key mapping (core/keymap.c) on its own, no engine: a note moved from one key to another, and by scale steps.
 * Run by tests/run_tests.sh. */
#include <stdio.h>
#include "../core/internal.h"

static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)
#define K(r, s) key_byte(r, s)

int main(void)
{
    int n;
    CHECK(key_root(K(7,5)) == 7 && key_scale(K(7,5)) == 5 && (K(7,5) & KEY_VALID), "key byte round trip");
    CHECK(key_map(64, K(0,1), K(0,2)) == 63, "C major E -> C minor Eb");
    CHECK(key_map(71, K(0,1), K(0,2)) == 70, "B -> Bb");
    CHECK(key_map(63, K(0,2), K(0,1)) == 64, "C minor Eb -> C major E");
    CHECK(key_map(61, K(0,1), K(0,2)) == 61, "C# (C + 1) stays C + 1");
    CHECK(key_map(60, K(11,1), K(0,1)) == 61, "B major -> C major: up a semitone, not down eleven");
    CHECK(key_map(60, K(0,1), K(11,1)) == 59, "C -> B: down a semitone");
    CHECK(key_map(62, K(0,1), K(2,6)) == 64, "C major D (degree 2) -> D dorian degree 2 = E");
    CHECK(key_map(64, K(0,3), K(2,3)) == 66, "PENTA to PENTA: shift by 2, E -> F#");
    CHECK(key_map(64, K(0,1), K(0,3)) == 64, "MAJOR -> PENTA: E stays (in C penta)");
    CHECK(key_map(65, K(0,1), K(0,3)) == 64, "F -> snapped down to E in C penta");
    CHECK(key_map(65, K(0,0), K(2,0)) == 67, "CHROM: shift only");
    CHECK(key_map(127, K(0,1), K(5,1)) == 127 && key_map(0, K(5,1), K(0,1)) >= 0, "clamped");
    CHECK(key_map(64, 0, K(0,2)) == 64 && key_map(64, K(0,1), 0) == 64, "no key: unchanged");
    CHECK(key_map(64, K(3,2), K(3,2)) == 64, "equal keys: unchanged");
    CHECK(key_steps(60, K(0,1), 3) == 65 && key_steps(64, K(0,1), 3) == 69 && key_steps(67, K(0,1), 3) == 72, "IV: C-E-G -> F-A-C");
    CHECK(key_steps(65, K(0,1), -3) == 60, "and back");
    CHECK(key_steps(61, K(0,0), 3) == 61, "CHROM: no steps");
    CHECK(key_steps(61, 0, 3) == 61, "no key: no steps");
    CHECK(key_steps(61, K(0,1), 7) == 73 && key_steps(73, K(0,1), -7) == 61, "an octave of steps, accidental kept");
    /* round trip: every note of C major's scale, 0..127, to C minor and back (accidentals need not round-trip:
       G# in C major is G + 1, which is Ab, a scale note in C minor, and comes back as A) */
    for (n = 0; n < 128; n++) if (SCALE_MASK[1] >> (n % 12) & 1)
        CHECK(key_map(key_map(n, K(0,1), K(0,2)), K(0,2), K(0,1)) == n, "round trip");
    printf("keymap: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
