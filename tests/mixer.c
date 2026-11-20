/* a22's track volume (the mixer): each of the four tape tracks has its own volume, set with EDIT held
 * and SELECT turned, shown as the INSTRUMENT ring, saved with the state. What is heard follows it: at
 * half the volume a note is the same sound at a sixteenth of the energy (the gain is the square of the
 * volume), at nothing or muted it is silent, and a turn of the knob does not click. Replaces h1's
 * tests/volume.c (a volume per instrument, which a22 does not have). Run by tests/run_tests.sh. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../core/internal.h"
#include "../core/h1.h"

static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

typedef struct Heard { double energy; int32_t peak, jump; } Heard;
static int32_t last_l;
static uint32_t ms;

static void go(int msec, Heard *h)
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i, j;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        for (j = 0; j < 512; j += 2) {
            int32_t s = out[j], a = s < 0 ? -s : s, d = s > last_l ? s - last_l : last_l - s;
            if (h) {
                h->energy += (double)s * (double)s;
                if (a > h->peak) h->peak = a;
                if (d > h->jump) h->jump = d;
            }
            last_l = s;
        }
        if (i % 4 == 0)
            h1_frame(ms += 23);
    }
}

static void turn(int d)                          /* EDIT held, SELECT turned, EDIT let go */
{
    h1_button(H1_BTN_EDIT, 1);
    h1_encoder(H1_ENC_SELECT, d);
    h1_button(H1_BTN_EDIT, 0);
}

static void fresh(void)
{
    h1_init(44100);
    h1_two_cores(0, 0);
    h1_control_set(H1_ENC_PRESET, 0);            /* FM-4, KEYS: a steady note */
    h1_control_set(H1_ENC_ALGO, 3);              /* PAD: it sustains */
    last_l = 0;
    ms = 0;
}

static void set_vol(int v)                       /* to v exactly, the way a hand would */
{
    int guard = 0;
    while (h1_instrument_volume(0) != v && guard++ < 400)
        turn(h1_instrument_volume(0) < v ? 1 : -1);
}

static double note_at(int vol)                   /* the energy of one held note, the volume settled first */
{
    Heard h;
    fresh();
    set_vol(vol);
    go(200, 0);                                  /* (the smoothing reaches the volume) */
    memset(&h, 0, sizeof h);
    h1_key(12, 1);
    go(400, &h);
    h1_key(12, 0);
    return h.energy;
}

int main(void)
{
    double full, half, none;

    /* the knob */
    fresh();
    CHECK(h1_instrument_volume(0) == 88, "a new track starts at 88");
    turn(-1000);
    CHECK(h1_instrument_volume(0) == 0, "EDIT + SELECT turned far down: 0");
    CHECK(h1_volume_ring() != 0, "turning it shows the ring");
    turn(1000);
    CHECK(h1_instrument_volume(0) == 100, "and far up: 100, no further");
    go(1700, 0);                                 /* (a state is written once the hand has been still 1.5 s) */
    CHECK(h1_state_dirty(), "a volume turned is a state to save");

    /* each track its own: T2 turned down leaves T1 as it was */
    fresh();
    set_vol(40);
    st.track = 1;
    CHECK(h1_instrument_volume(0) == 88, "T2 is untouched by T1's volume");
    st.track = 0;
    CHECK(h1_instrument_volume(0) == 40, "T1 keeps its own");

    /* saved and brought back */
    {
        static uint8_t buf[4096];
        uint32_t size;
        fresh();
        set_vol(23);
        { const void *src_ = h1_state(&size); memcpy(buf, src_, size); }   /* (size set first: argument order is unspecified) */
        fresh();
        CHECK(h1_state_load(buf, size) == 0, "the state loads");
        CHECK(h1_instrument_volume(0) == 23, "the volume comes back as it was saved");
        buf[offsetof(H1State, trk) + offsetof(Track, vol)] = 250;
        CHECK(h1_state_load(buf, size) == 0 && h1_instrument_volume(0) == 88,
              "nonsense where the volume is: the default, not a track gone quiet");
    }

    /* what is heard */
    full = note_at(100);
    half = note_at(50);
    none = note_at(0);
    CHECK(full > 1e9, "at full a note is heard");
    CHECK(half > full * 0.05 && half < full * 0.075, "at 50 it is a sixteenth as loud (the gain is the volume squared)");
    CHECK(none == 0.0, "at nothing it is silent");
    if (full > 0) printf("mixer: 50 is %.4f of full's energy, 0 is %.4f\n", half / full, none / full);

    /* muted: silent, whatever the volume */
    {
        Heard h;
        fresh();
        st.trk[0].mute = 1;
        go(200, 0);
        memset(&h, 0, sizeof h);
        h1_key(12, 1);
        go(300, &h);
        h1_key(12, 0);                           /* (ui.c keeps a key's down through h1_init: let it go) */
        CHECK(h.energy == 0.0, "a muted track is silent");
    }

    /* a turn while a note sounds does not click: no sample steps further than the note itself does */
    {
        Heard steady, turning;
        int i;
        fresh();
        set_vol(100);
        h1_key(12, 1);
        go(300, 0);
        memset(&steady, 0, sizeof steady);
        go(300, &steady);
        memset(&turning, 0, sizeof turning);
        for (i = 0; i < 10; i++) {               /* 100 down to 0 in ten quick turns */
            turn(-10);
            go(12, &turning);
        }
        go(200, &turning);
        h1_key(12, 0);
        CHECK(steady.jump > 0, "the note moves");
        CHECK(turning.jump <= steady.jump + steady.jump / 8, "turning the volume down under a note does not click");
        printf("mixer: largest step steady %d, while turning %d\n", steady.jump, turning.jump);
    }

    printf("mixer: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
