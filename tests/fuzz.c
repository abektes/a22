/* h1's crash hunt: the core alone, on a computer, under the compiler's checkers.
 *
 *   sh tests/run_fuzz.sh [SEEDS]
 *
 * The FM-1's CPU stops on things a computer shrugs at: a word read from an odd address, a read
 * past the end of an array, a rectangle taller than the buffer it is converted in. Built with
 * -fsanitize=address,undefined, every one of those stops this program too, with the line.
 * It does four things, each from a seed so a failure can be run again:
 *
 *   1. state: saved states that pass the magic and version check but hold any bytes at all, handed
 *      over from an odd address, then played. Whatever a store gives back, h1 has to start.
 *   2. panel: every key, button (taps and holds), encoder and MIDI byte in any order and at any
 *      pace, on every instrument and page, with the sound rendered and every frame drawn.
 *   2b. long notes: every algorithm of every instrument with the knobs at their ends and a chord held
 *      nine seconds, then let go: what only goes wrong late in a note.
 *   3. two threads: the same, while a second thread renders the sound without waiting its turn,
 *      as the audio interrupt does on the device.
 *
 * It checks what the checkers cannot see: every rectangle sent to the panel is inside the screen
 * and no taller than a band (20 rows), after any state the values a picture and a sound index
 * with are in range, and a frame sent the way the device sends it (in pieces, while the next one is
 * drawn) leaves the panel showing exactly that frame. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/h1.h"

static uint32_t rng_s = 1;
static uint32_t rnd(void)
{
    rng_s ^= rng_s << 13;
    rng_s ^= rng_s >> 17;
    rng_s ^= rng_s << 5;
    return rng_s;
}
static int below(int n) { return (int)(rnd() % (uint32_t)n); }

static unsigned seed_now;
static const char *part = "";
static void die(const char *what, int a, int b, int c, int d)
{
    fprintf(stderr, "FAIL (%s, seed %u): %s %d %d %d %d\n", part, seed_now, what, a, b, c, d);
    exit(1);
}

static void blit(int x, int y, int w, int h)
{
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > H1_W || y + h > H1_H || h > 20)
        die("a rectangle the device cannot take", x, y, w, h);
}

extern uint32_t h1_fuzz_nan;                 /* core/synth.c, in this build: samples that were not numbers */

static uint32_t now_ms;
static uint32_t keys_down, btns_down;

static void sound(uint32_t n)
{
    static int32_t lr[2 * 1024];
    uint32_t i;
    h1_render(lr, n);
    if (h1_fuzz_nan)
        die("the sound is not a number", (int)h1_fuzz_nan, h1_instrument(), h1_algorithm(), 0);
    for (i = 0; i < 2 * n; i++)
        if (lr[i] > 8388607 || lr[i] < -8388608)
            die("a sample outside 24 bits", (int)i, (int)lr[i], 0, 0);
}

/* The device sends a frame while the next one is drawn (h1_present_begin, _next, h1_while_drawing).
 * Here the same, with a panel of our own: rectangles leave the queue at any moment of the next frame's
 * drawing, and when the queue is empty the panel has to show exactly the frame that was queued. */
static uint16_t shown[H1_W * H1_H], want[H1_W * H1_H], pal_then[256];
static int piped, have_want;

static void pipe_one(void)
{
    const uint8_t *px = h1_panel();
    int x, y, w, h, i, j;
    if (!h1_present_next(&x, &y, &w, &h))
        return;
    blit(x, y, w, h);
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            shown[j * H1_W + i] = pal_then[px[j * H1_W + i]];
}

static void pipe_poll(void)
{
    if (piped && below(4) == 0)
        pipe_one();
}

static void frame(uint32_t ms, int with_sound)
{
    now_ms += ms;
    if (with_sound)
        sound(ms > 16 ? 256u * 3u : 256u * (uint32_t)(1 + below(3)));
    h1_frame(now_ms);
    if (piped) {
        const uint8_t *fb = h1_framebuffer();
        int i;
        while (h1_present_left())
            pipe_one();
        if (have_want && memcmp(shown, want, sizeof shown))
            die("the panel does not show the frame that was sent", 0, 0, 0, 0);
        memcpy(pal_then, h1_palette(), sizeof pal_then);
        h1_present_begin();
        if (h1_present_rects() > 512 || (h1_panel() == fb))
            die("the queue", h1_present_rects(), 0, 0, 0);
        for (i = 0; i < H1_W * H1_H; i++)
            want[i] = pal_then[fb[i]];
        have_want = 1;
    } else {
        h1_present(blit);
        have_want = 0;
    }
    if (h1_instrument() < 0 || h1_instrument() >= h1_instrument_count() || h1_algorithm() < 0 || h1_algorithm() > 7 ||
        h1_page() < 0 || h1_page() > 7)
        die("out of range", h1_instrument(), h1_algorithm(), h1_page(), 0);
    (void)h1_key_leds();
    (void)h1_button_leds();
    {
        int k;
        for (k = 0; k < H1_NENC; k++) {
            int lo, hi;
            const char *name = 0, *text = 0;
            int v = h1_control(k, &lo, &hi, &name, &text);
            if (name && strlen(name) > 40) die("a name too long", k, v, 0, 0);
            if (text && strlen(text) > 40) die("a value's text too long", k, v, 0, 0);
        }
    }
}

/* one thing a hand (or a cable) could do */
static void poke(int with_sound)
{
    int r = below(100), k;
    if (r < 30) {                                            /* a key */
        k = below(H1_NKEYS);
        keys_down ^= 1u << k;
        h1_key(k, (int)(keys_down >> k & 1u));
    } else if (r < 50) {                                     /* a button down or up */
        k = below(H1_NBTN);
        btns_down ^= 1u << k;
        h1_button(k, (int)(btns_down >> k & 1u));
    } else if (r < 75) {                                     /* an encoder: a click, or a spin */
        static const int D[] = {1, -1, 1, -1, 2, -3, 7, -11, 40, -64, 300, -300};
        h1_encoder(below(H1_NENC), D[below(12)]);
    } else if (r < 85) {                                     /* MIDI, any bytes */
        h1_midi((uint8_t)(0x80 + below(128)), (uint8_t)below(256), (uint8_t)below(256));
    } else if (r < 90) {                                     /* a slider in the simulator, any value */
        h1_control_set(below(H1_NENC), below(600) - 200);
    } else if (r < 93) {                                     /* everything let go */
        for (k = 0; k < H1_NKEYS; k++)
            if (keys_down >> k & 1u) h1_key(k, 0);
        for (k = 0; k < H1_NBTN; k++)
            if (btns_down >> k & 1u) h1_button(k, 0);
        keys_down = btns_down = 0;
    } else if (r < 94) {                                     /* the volume knob, in range and out */
        h1_master(below(1400) - 200);
    } else if (r < 96) {                                     /* nonsense from a port: ignored */
        h1_key(below(64) - 16, below(2));
        h1_button(below(64) - 16, below(2));
        h1_encoder(below(32) - 8, below(9) - 4);
        h1_control_set(below(32) - 8, below(600) - 200);
    } else {                                                 /* time passes: long enough for any hold */
        static const uint32_t T[] = {0, 1, 100, 500, 1000, 3200, 6000};
        frame(T[below(7)], with_sound);
    }
}

static void fresh(uint32_t t0)
{
    h1_init(44100);
    h1_while_drawing(pipe_poll);
    have_want = 0;
    piped = (int)(rnd() & 1u);                   /* half the time as the device sends, half all at once */
    now_ms = t0;
    keys_down = btns_down = 0;
}

/* ---- 1. any saved state, from an odd address ----------------------------------------------- */
static void part_state(int rounds)
{
    static uint8_t odd[4096 + 8];
    uint32_t size, i;
    int n, k;
    part = "state";
    fresh(0);
    (void)h1_state(&size);
    if (size + 3 > sizeof odd)
        die("the state outgrew this test", (int)size, 0, 0, 0);
    /* a good state comes back, wherever the buffer sits */
    for (k = 1; k < 4; k++) {
        fresh(0);
        h1_encoder(H1_ENC_PRESET, 3 + k);
        memcpy(odd + k, h1_state(&size), size);
        fresh(0);
        if (h1_state_load(odd + k, size) != 0 || h1_instrument() != 3 + k)
            die("a good state was refused at an odd address", k, h1_instrument(), 0, 0);
    }
    /* what is not this version's is refused and changes nothing */
    fresh(0);
    memcpy(odd + 1, h1_state(&size), size);
    odd[1] ^= 1u;
    if (h1_state_load(odd + 1, size) == 0 || h1_state_load(odd + 1, size - 1) == 0 || h1_state_load(0, size) == 0)
        die("a bad state was taken", 0, 0, 0, 0);
    for (n = 0; n < rounds; n++) {
        fresh(n & 1 ? 0xFFFFF000u : 0u);                     /* (the clock's wrap too) */
        memcpy(odd + 1, h1_state(&size), size);
        if (n % 3 == 0)
            for (i = 5; i < size; i++) odd[1 + i] = (uint8_t)rnd();          /* all of it */
        else
            for (k = 0; k < 40; k++) odd[1 + 5 + below((int)size - 5)] = (uint8_t)rnd();   /* a few bytes */
        if (n % 5 == 4) {                                    /* the state before LAYERS: version 4, and shorter */
            odd[1 + 4] = 4;
            if (h1_state_load(odd + 1, size - 256u) != 0 || h1_layers())
                die("a version 4 state was refused, or came back with LAYERS on", n, h1_layers(), 0, 0);
        } else if (h1_state_load(odd + 1, size) != 0)
            die("a state with the right magic and version was refused", n, 0, 0, 0);
        for (k = 0; k < 6; k++)
            frame(16, 1);
        h1_button(H1_BTN_PLAY, 1);                           /* whatever the pattern holds, play it */
        h1_button(H1_BTN_PLAY, 0);
        for (k = 0; k < 4; k++)
            h1_key(below(H1_NKEYS), 1);
        for (k = 0; k < 40; k++)
            frame(16 + (uint32_t)(k % 3 == 0), 1);
        for (k = 0; k < 60; k++)
            poke(1);
    }
}

/* ---- 2. the panel, any way at all ------------------------------------------------------------ */
static void part_panel(int steps)
{
    int n, inst;
    part = "panel";
    for (inst = 0; inst < h1_instrument_count(); inst++) {
        fresh(inst & 1 ? 0xFFFF0000u : 0u);
        h1_encoder(H1_ENC_PRESET, inst);
        if (inst & 2) {                                      /* half of them with LAYERS on from the start */
            h1_button(H1_BTN_SEQ, 1);
            h1_button(H1_BTN_REC, 1);
            h1_button(H1_BTN_REC, 0);
            h1_button(H1_BTN_SEQ, 0);
            if (!h1_layers())
                die("SEQ and REC together did not turn LAYERS on", inst, 0, 0, 0);
        }
        for (n = 0; n < steps; n++) {
            poke(1);
            if (n % 4 == 0)
                frame(16 + (uint32_t)(n % 3 == 0), 1);
        }
    }
}

/* ---- 2b. long notes: every algorithm of every instrument, a chord held nine seconds ----------- */
static void part_long(void)
{
    static const uint8_t EDGE[] = {0, 0, 50, 100, 100, 127};
    int inst, algo, k, n;
    part = "long notes";
    fresh(0);
    for (inst = 0; inst < h1_instrument_count(); inst++) {
        for (algo = 0; algo < 8; algo++) {
            for (k = 0; k < 4; k++)                          /* the knobs at their ends, mostly */
                h1_midi(0xB0, (uint8_t)(20 + k), below(4) ? EDGE[below(6)] : (uint8_t)below(128));
            for (k = 0; k < 3; k++)
                h1_key(below(H1_NKEYS), 1);
            for (n = 0; n < 390; n++) {                      /* 390 * 1024 frames: nine seconds */
                sound(1024);
                if (n % 16 == 0)
                    frame(370, 0);
            }
            for (k = 0; k < H1_NKEYS; k++)
                h1_key(k, 0);
            for (n = 0; n < 40; n++)                         /* and the tails */
                sound(1024);
            frame(16, 1);
            h1_encoder(H1_ENC_ALGO, 1);
        }
        h1_encoder(H1_ENC_PRESET, 1);
        frame(16, 1);
    }
}

/* ---- 3. the sound on its own thread ---------------------------------------------------------- */
static volatile int audio_run;
static void *audio_thread(void *arg)
{
    static int32_t lr[2 * 256];
    (void)arg;
    while (audio_run)
        h1_render(lr, 256);
    return 0;
}

static void part_threads(int steps)
{
    pthread_t th;
    int n;
    part = "two threads";
    fresh(0);
    audio_run = 1;
    if (pthread_create(&th, 0, audio_thread, 0))
        die("no thread", 0, 0, 0, 0);
    for (n = 0; n < steps; n++) {
        poke(0);
        if (n % 4 == 0)
            frame(16, 0);
    }
    audio_run = 0;
    pthread_join(th, 0);
    if (h1_fuzz_nan)
        die("the sound is not a number", (int)h1_fuzz_nan, h1_instrument(), h1_algorithm(), 0);
}

int main(int argc, char **argv)
{
    unsigned seeds = argc > 1 ? (unsigned)atoi(argv[1]) : 8u, first = argc > 2 ? (unsigned)atoi(argv[2]) : 1u, s;
    for (s = first; s < first + seeds; s++) {
        seed_now = s;
        rng_s = s * 2654435761u + 1u;
        part_state(40);
        part_panel(1500);
        part_long();
        part_threads(6000);
    }
    printf("fuzz: %u seeds from %u, nothing found\n", seeds, first);
    return 0;
}
