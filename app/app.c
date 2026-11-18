/* h1 on the FM-1 (fm1-base). The whole firmware is the portable core in ../core; this file only
 * passes the panel in, and the sound, the lights and the changed parts of the picture out.
 *
 * NOT YET BUILT FOR THE DEVICE. It follows fm1-base's template (plat.h, lcd_blit, the store) and
 * has to be compiled with the JieLi toolchain, checked for RAM (see docs/ARCHITECTURE.md) and
 * tried on hardware before anyone flashes it. */
#include "app.h"
#include "../core/h1.h"

/* ---- the screen: 8-bit ink through the palette into RGB565, band by band -------------------- */
#define BAND_ROWS 20                     /* the core never asks for a taller rectangle */
static uint16_t band[2][240 * BAND_ROWS] PLAT_POOL;   /* two, so one can go out while the next is filled */
static uint16_t pal_be[256];             /* the palette as the panel wants it: big-endian */
static uint16_t pal_seen[2];
static uint32_t band_i;

static void palette_sync(void)
{
    const uint16_t *pal = h1_palette();
    uint32_t i;
    if (pal[255] == pal_seen[0] && pal[128] == pal_seen[1])
        return;
    pal_seen[0] = pal[255];
    pal_seen[1] = pal[128];
    for (i = 0; i < 256u; i++)
        pal_be[i] = (uint16_t)((pal[i] >> 8) | (pal[i] << 8));
}

static void blit(int x, int y, int w, int h)
{
    const uint8_t *src = h1_framebuffer() + y * H1_W + x;
    uint16_t *dst = band[band_i];
    int i, j;
    for (j = 0; j < h; j++, src += H1_W)
        for (i = 0; i < w; i++)
            *dst++ = pal_be[src[i]];
    lcd_blit((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, band[band_i]);
    band_i ^= 1u;
}

/* ---- the panel ------------------------------------------------------------------------------ */
static uint32_t keys_was, btn_was;

/* MIDI in (USB and the TRS jack): read here, in the main loop, because the core's event queue has one
 * writer and the panel is it */
static void midi_in(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port))
        h1_midi((uint8_t)(pkt >> 8), (uint8_t)(pkt >> 16), (uint8_t)(pkt >> 24));
}

static void speak(void);

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), k;
    for (k = 0; k < NKEYS; k++)
        if ((keys ^ keys_was) >> k & 1u)
            h1_key((int)k, (int)(keys >> k & 1u));
    for (k = 0; k < NB; k++)
        if ((btn ^ btn_was) >> k & 1u)
            h1_button((int)k, (int)(btn >> k & 1u));       /* B_* and H1_BTN_* are the same order */
    for (k = 0; k < NE; k++) {
        int32_t d = plat_enc((int)k);                       /* EN_* and H1_ENC_* too */
        if (d)
            h1_encoder((int)k, (int)d);
    }
    keys_was = keys;
    btn_was = btn;
    midi_in();
}

/* ---- fm1-base's calls ----------------------------------------------------------------------- */
void app_init(void)
{
    static uint8_t blob[1024];
    uint32_t size;
    int n;
    h1_init(44100);
    h1_state(&size);
    n = size <= sizeof blob ? plat_store_load(OBJ_STATE, blob, sizeof blob) : -1;
    if (n == (int)size)
        h1_state_load(blob, size);                          /* refused unless it is this version's */
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float gain_now;
    float gain = (float)master_q12 * (float)master_q12 * (1.0f / (4096.0f * 4096.0f));   /* MASTER, as a square */
    uint32_t i;
    h1_render(lr, n);                                       /* 24-bit, already limited */
    for (i = 0; i < n; i++) {
        gain_now += (gain - gain_now) * 0.004f;             /* ~5 ms: turning MASTER does not click */
        lr[2 * i] = (int32_t)((float)lr[2 * i] * gain_now);
        lr[2 * i + 1] = (int32_t)((float)lr[2 * i + 1] * gain_now);
    }
}

void app_panic(void) { h1_midi(0xB0, 123, 0); }

void app_ui_init(void) { lcd_fill(0, 0, 240, 240, 0); }

void app_frame(void)
{
    input();
    h1_frame(plat_ms());
    palette_sync();
    h1_present(blit);
    plat_glow(1);
    plat_leds(h1_button_leds(), h1_key_leds());
    speak();
    /* saving erases flash, which stalls the sound for a moment: only when nothing is sounding */
    if (h1_state_dirty() && !h1_voices_active()) {
        uint32_t size;
        const void *p = h1_state(&size);
        if (plat_store_save(OBJ_STATE, p, size) == 0)
            h1_state_saved();
    }
}

void app_input_only(void) { input(); }

/* ---- a screen reader (fm1-reader) ------------------------------------------------------------
 * The picture is not text, so the reader hears what the controls are set to: the instrument and
 * its algorithm when either changes, the four knobs' names when a page opens, and a control's
 * name and value once it has stopped turning. */
static char *say_cat(char *d, char *end, const char *s)
{
    while (s && *s && d < end)
        *d++ = *s++;
    *d = 0;
    return d;
}

static void speak(void)
{
    static int inst_was = -1, algo_was = -1, page_was = -1, val_was[H1_NENC];
    char t[96], *d, *end = t + sizeof t - 1;
    int inst = h1_instrument(), algo = h1_algorithm(), page = h1_page(), k, quiet = 0;
    if (ann_on()) {
        int where = ann_where();
        if (where || inst != inst_was || algo != algo_was) {
            d = say_cat(t, end, where ? APP_NAME ". " : "");
            d = say_cat(d, end, h1_instrument_name(inst));
            d = say_cat(d, end, ". ");
            say_cat(d, end, h1_algo_name(inst, algo));
            ann_say(t, 0, ANN_NOW);
            quiet = 1;                                      /* the knobs all changed with it: not said */
        }
        if (where || page != page_was) {
            d = say_cat(t, end, page ? "PAGE" : "HOME");
            for (k = H1_ENC_K1; k <= H1_ENC_K4; k++) {
                const char *name = 0;
                h1_control(k, 0, 0, &name, 0);
                d = say_cat(d, end, ". ");
                d = say_cat(d, end, name);
            }
            ann_say(t, 0, quiet ? 0 : ANN_NOW);
            quiet = 1;
        }
        for (k = 0; k < H1_NENC; k++) {
            const char *name = 0, *text = 0;
            int v = h1_control(k, 0, 0, &name, &text);
            if (v != val_was[k] && !quiet && k != H1_ENC_PRESET && k != H1_ENC_ALGO) {
                d = say_cat(t, end, name);
                d = say_cat(d, end, " ");
                say_cat(d, end, text);
                ann_later(t, ANN_NOW, 250u, plat_ms());
            }
            val_was[k] = v;
        }
    }
    inst_was = inst;
    algo_was = algo;
    page_was = page;
    ann_tick(plat_ms());
}

/* the platform's own short messages (an update starting, a save that failed): to the reader */
void app_say(const char *a, const char *b)
{
    if (ann_on())
        ann_say(a, b, ANN_NOW | ANN_KEEP);
}
