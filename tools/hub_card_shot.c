/* One screen of A22 for The Firmware Hub's card (tools/hub_card.py), rendered by the core itself and written as a
 * PPM: an engine playing, after every name box has cleared. One per process (the core keeps some state through
 * h1_init).
 *   hub_card_shot ENGINE OUT.ppm     a chord held (a beat for KIT and SKINS)
 *   hub_card_shot tape OUT.ppm       four tracks on the tape, its page open */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal.h"
#include "h1.h"

#define SR 44100
static long samples, frame_no;

static void go(int msec)
{
    static int32_t out[512];
    long until = samples + (long)msec * SR / 1000;
    while (samples < until) {
        h1_render(out, 256);
        samples += 256;
        while (samples * 60 / SR > frame_no) {
            frame_no++;
            h1_frame((uint32_t)(frame_no * 1000 / 60));
            (void)h1_present(0);
        }
    }
}

static void ppm(const char *path)
{
    const uint8_t *fb = h1_framebuffer();
    const uint16_t *pal = h1_palette();
    FILE *f = fopen(path, "wb");
    int i;
    if (!f) exit(1);
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240 * 240; i++) {
        uint16_t c = pal[fb[i]];
        uint8_t rgb[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63),
                          (uint8_t)((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void keys(const int *k, int n, int down) { int i; for (i = 0; i < n; i++) h1_key(k[i], down); }
static void btn(int b) { h1_button(b, 1); go(60); h1_button(b, 0); go(60); }

int main(int argc, char **argv)
{
    static const int chord[4] = {0, 4, 7, 11}, high[4] = {7, 11, 14, 18};
    if (argc < 3) return 2;
    h1_init(SR);
    st.seq[1] = 0;
    go(400);
    if (!strcmp(argv[1], "tape")) {               /* four tracks of sixteenths: stabs, an arpeggio, a beat, a bass */
        static const int ins[4] = {2, 0, 8, 7};
        int i, t;
        for (t = 0; t < 4; t++) {
            if (t) { h1_button(H1_BTN_EDIT, 1); h1_encoder(H1_ENC_PRESET, 1); h1_button(H1_BTN_EDIT, 0); go(300); }
            h1_encoder(H1_ENC_PRESET, ins[t] - h1_instrument());
            go(300);
            btn(H1_BTN_REC);
            for (i = 0; i < 16; i++) {
                int k = t == 0 ? (i % 4 == 0 || i % 4 == 3 ? 7 : -1)
                      : t == 1 ? 14 + (int[]){0, 4, 7, 11}[i % 4]
                      : t == 2 ? (i % 4 == 0 ? 7 : i % 8 == 4 ? 9 : 13)
                      : (i % 2 == 0 ? (int[]){0, 0, 3, 5}[(i / 4) % 4] : -1);
                if (k >= 0) {
                    if (t == 0) keys(chord, 4, 1); else h1_key(k, 1);
                    go(40);
                    if (t == 0) keys(chord, 4, 0); else h1_key(k, 0);
                    go(60);
                } else go(100);                  /* (16 steps inside the one bar, ~1.7 s of 2.03: REC closes it) */
            }
            btn(H1_BTN_REC);
            go(2200);                            /* (a loop round before the next track: the take settles) */
        }
        btn(H1_BTN_SEQ);                         /* the tape's page */
        go(1700);
        ppm(argv[2]);
        return 0;
    }
    {
        int e = atoi(argv[1]);
        h1_encoder(H1_ENC_PRESET, e - h1_instrument());
        go(1800);                                /* (the engine's name and its algorithm's box gone) */
        if (e == 13 || e == 8) {                 /* drums: a kick, the snare, hats, the last just before the shot */
            static const int hits[8] = {7, 13, 9, 13, 17, 11, 12, 14};   /* ... and toms last: the skin dented */
            int i;
            for (i = 0; i < 8; i++) { h1_key(hits[i], 1); go(40); h1_key(hits[i], 0); go(110); }
            h1_key(7, 1); h1_key(9, 1); h1_key(13, 1); go(45);
        } else {
            keys(e == 9 ? high : chord, 4, 1);
            go(e == 9 ? 260 : 1200);
        }
        ppm(argv[2]);
    }
    return 0;
}
