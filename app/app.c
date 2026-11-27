/* h1 on the FM-1 (fm1-base). The whole firmware is the portable core in ../core; this file only
 * passes the panel in, and the sound, the lights and the changed parts of the picture out. */
#include "app.h"
#include "../core/h1.h"
#include "midi_tiers.h"

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
    if (pal[31] == pal_seen[0] && pal[255] == pal_seen[1])
        return;
    pal_seen[0] = pal[31];
    pal_seen[1] = pal[255];
    for (i = 0; i < 256u; i++)
        pal_be[i] = (uint16_t)((pal[i] >> 8) | (pal[i] << 8));
}

/* The panel takes a full screen in 21 ms, as long as a frame takes to draw. So a frame goes out while
 * the next one is drawn: the core queues what changed (h1_present_begin) and calls send_some from inside
 * its drawing, many times a frame. Each call does one small thing: fills the free band from the next
 * queued rectangle, or hands a filled band to the panel if the panel has finished with the last one. */
static int sq[4];                        /* the rectangle in the filled band: x, y, w, h */
static uint8_t sq_full;
static struct { uint32_t bytes, rects; } sent;

#ifndef PLAT_HOST
#include "hw/panel.h"
#endif

static int panel_idle(void)
{
#ifdef PLAT_HOST
    return 1;
#else
    return !lcd_busy || hw_panel_done();     /* (fm1-base's device/lcd.c: no transfer running, or it is done) */
#endif
}

static void band_fill(void)
{
    const uint8_t *src;
    uint16_t *dst = band[band_i];
    int i, j;
    if (!h1_present_next(&sq[0], &sq[1], &sq[2], &sq[3]))
        return;
    src = h1_panel() + sq[1] * H1_W + sq[0];
    for (j = 0; j < sq[3]; j++, src += H1_W)
        for (i = 0; i < sq[2]; i++)
            *dst++ = pal_be[src[i]];
    sq_full = 1;
}

static void band_send(void)              /* (waits for the panel if it is still busy) */
{
    lcd_blit((uint32_t)sq[0], (uint32_t)sq[1], (uint32_t)sq[2], (uint32_t)sq[3], band[band_i]);
    band_i ^= 1u;                        /* the other band is free: the transfer before this one is over */
    sq_full = 0;
}

static void input_drawing(void);

static void send_some(void)
{
    input_drawing();
    if (!sq_full)
        band_fill();
    else if (panel_idle())
        band_send();
}

static void send_rest(void)
{
    for (;;) {
        if (!sq_full)
            band_fill();
        if (!sq_full)
            return;
        band_send();
    }
}

/* ---- how long things take ---------------------------------------------------------------------
 * Measured all the time (three clock reads a frame, two a block of sound). MIDI CC 119 from the
 * computer makes a listening screen reader say the numbers since the last time it was asked:
 *   PERF. FPS n. DRAW avg MAX max. SEND avg MAX max. AUDIO avg MAX max. KB n RECTS n. STACK main interrupt
 * DRAW and SEND are milliseconds a frame (the picture drawn, with most of the frame before it sent meanwhile;
 * then the rest of that frame sent and this one's changes found); KB and RECTS are what a frame sends;
 * AUDIO is the share of each block's time the sound took, in percent; STACK is the bytes of the main
 * loop's and the interrupts' stacks that have never been used since the start.
 * Then STAGE seq voices wait mix fx master: the slowest render since the last PERF, split by h1_stages, each in
 * percent of the block's time (they add up to that render's time in h1_render; AUDIO also counts the port's own
 * MASTER gain after it, so it reads a little higher); and FLASH n max: the tape's flash writes in that time and the
 * longest in microseconds (each holds the interrupts off, the audio's included: ring_write); and ERASE n max: the
 * ring's 4 KiB sector erases from frames in that time, and the most erase time in one frame, in microseconds (one
 * sector's at most since erases go a sector a frame: ring_erase_some; the start's, before the sound, not counted).
 * The stages are timed only from the first CC 119 on (stage_clock_on), so a unit nobody profiles reads no clock in
 * its render. */
#define US(t) ((t) / PLAT_TICKS_PER_US)
static struct { uint32_t frames, draw, draw_max, send, send_max, since_ms, flash_n, flash_max, erase_n, erase_max; } perf;
static uint32_t erase_us_frame;                 /* this frame's erase time so far (ERASE) */
static volatile uint32_t pf_blocks, pf_audio, pf_audio_max, pf_block_us = 5805u;
/* the renders as the tape's flash writes see them (ring_go): one more each render, and the slowest of the last few
 * (each render takes an eighth off it), in microseconds */
static volatile uint32_t au_seq, au_recent;
static uint8_t pf_ask, stage_clock_on;

/* ---- the panel ------------------------------------------------------------------------------ */
static uint32_t keys_was, btn_was;

/* MIDI in (USB and the TRS jack): read here, in the main loop, because the core's event queue has one
 * writer and the panel is it */
static uint32_t midi_held[32];          /* read while a frame was drawn, played at the frame's input (input_drawing) */
static uint32_t n_midi_held;

static void midi_one(uint32_t pkt)
{
    if (((pkt >> 8) & 0xF0u) == 0xB0u && ((pkt >> 16) & 0xFFu) == 119u) {
        pf_ask = 1;                                         /* (see "how long things take") */
        if (!stage_clock_on) {                              /* the stages timed from now on (STAGE) */
            h1_stage_clock(plat_ticks24);
            stage_clock_on = 1;
        }
    } else
        h1_midi((uint8_t)(pkt >> 8), (uint8_t)(pkt >> 16), (uint8_t)(pkt >> 24));
}

static void midi_in(void)
{
    uint32_t pkt, i;
    int port;
    for (i = 0; i < n_midi_held; i++)
        midi_one(midi_held[i]);
    n_midi_held = 0;
    while (plat_midi_in(&pkt, &port))
        midi_one(pkt);
}

/* Keys and MIDI notes while a frame is drawn (called from send_some, which the drawing calls many times a frame):
 * a busy jam draws at 15 to 20 frames a second, and read once a frame a key waited up to 50 ms or more. A note only
 * posts to the core's queue (the main loop stays its one writer). What could change what the frame is drawing is
 * left to the frame's input: keys while a button is held (SAVE + a key is a snapshot), and MIDI from the first
 * message that is not a note on, with all after it, kept in order and played there. At most once a millisecond. */
static void input_drawing(void)
{
    static uint32_t at;
    uint32_t now = plat_ms(), keys, k, pkt;
    int port;
    if (now == at)
        return;
    at = now;
    if (!btn_was && !plat_buttons()) {
        keys = plat_keys();
        for (k = 0; k < NKEYS; k++)
            if ((keys ^ keys_was) >> k & 1u)
                h1_key((int)k, (int)(keys >> k & 1u));
        keys_was = keys;
    }
    while (n_midi_held < sizeof midi_held / sizeof midi_held[0] && plat_midi_in(&pkt, &port)) {
        uint32_t type = (pkt >> 8) & 0xF0u;
        if (!n_midi_held && (type == 0x90u || type == 0x80u))
            h1_midi((uint8_t)(pkt >> 8), (uint8_t)(pkt >> 16), (uint8_t)(pkt >> 24));
        else
            midi_held[n_midi_held++] = pkt;
    }
}

static void speak(void);

/* USB host (fm1.toml usb_host): the core asks with OCT- and OCT+ held (h1_usb_host_fn), the platform switches
 * the port, and what it finds is told as a toast. A device lost, or the mode changed: its notes let go. */
static void usb_host(int on) { plat_usb_host(on); }
static void usb_tell(const char *s)             /* on the screen, and to the reader */
{
    h1_toast(s);
    if (ann_on())
        ann_say(s, 0, ANN_NOW);
}
static void usb_news(void)
{
    static int host_was;
    static uint32_t link_was;
    if (plat_midi_out_link() != link_was) {      /* a new USB connection: a DAW that joins while the tape plays gets
                                                  * Stop and Start when it next comes round (h1_midi_resync) */
        link_was = plat_midi_out_link();
        h1_midi_resync();
    }
    if (h1_usb_host() != host_was) {             /* (the core's own toast says it on the screen) */
        host_was = h1_usb_host();
        if (ann_on())
            ann_say(host_was ? "USB HOST" : "USB COMPUTER", 0, ANN_NOW);
    }
    switch (plat_usb_host_msg()) {
    case USBH_MSG_MIDI: {
        const char *nm = plat_usb_host_name();
        usb_tell(nm && nm[0] ? nm : "KEYBOARD");
        break;
    }
    case USBH_MSG_NOMIDI: usb_tell("NOT MIDI"); break;
    case USBH_MSG_LOW: usb_tell("NOT SUPPORTED"); break;
    case USBH_MSG_LOST: usb_tell("UNPLUGGED"); break;
    default: break;
    }
    if (plat_usb_lost())
        h1_midi(0xB0, 123, 0);
}

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
/* A start that crashes or hangs in its first 30 seconds is counted by the platform, and two in a row are
 * safe mode. After one, the next start leaves the saved state alone and comes up as new, so nothing h1
 * has saved can keep it from starting. (The save itself stays until the next change is saved over it.) */
#ifdef PLAT_HOST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void ring_dump(void);
static int proj_host_cmd(const char *cmd, const char *a, const char *b);
static int host_bad_start;                                  /* the tests' "badstart": the next start is one */
static uint32_t host_er_n, host_er_frame, host_er_max;      /* the ring's sector erases from frames: in all, this
                                                             * frame's, the most in one frame (expect ring_erases) */
static void ring_boot_state(void);
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)c; (void)d;
    if (!strcmp(cmd, "expect") && !strcmp(a, "loop_size")) {   /* expect loop_size N: the loop's saved bytes */
        uint32_t size = 0;
        (void)h1_loop(&size);
        if (size != (uint32_t)strtoul(b, 0, 0)) {
            printf("FAIL expect loop_size == %s, got %u\n", b, (unsigned)size);
            exit(1);
        }
        printf("  ok loop_size == %u\n", (unsigned)size);
        return 1;
    }
    if (!strcmp(cmd, "ring")) {                  /* ring: where the loop's record is, and each slot's fill */
        ring_dump();
        return 1;
    }
    if (proj_host_cmd(cmd, a, b))                /* proj save N, proj, expect proj.N / proj_same N / proj_saves N */
        return 1;
    if (!strcmp(cmd, "expect") && !strcmp(a, "ring_erases")) {   /* expect ring_erases MIN MAX_A_FRAME: the ring's sector
                                                                  * erases since the start, at least MIN, and at most
                                                                  * MAX_A_FRAME of them in one frame */
        uint32_t lo = (uint32_t)strtoul(b, 0, 0), hi = (uint32_t)strtoul(c, 0, 0);
        if (host_er_n < lo || host_er_max > hi) {
            printf("FAIL expect ring_erases >= %s, <= %s a frame: got %u, %u a frame\n", b, c,
                   (unsigned)host_er_n, (unsigned)host_er_max);
            exit(1);
        }
        printf("  ok ring_erases %u, at most %u a frame\n", (unsigned)host_er_n, (unsigned)host_er_max);
        return 1;
    }
    if (strcmp(cmd, "badstart"))
        return 0;
    host_bad_start = 1;
    return 1;
}
static int last_start_failed(void)
{
    int f = host_bad_start;
    host_bad_start = 0;
    if (f)
        ring_boot_state();                       /* (as the device has it after a restart: the host keeps statics) */
    return f;
}
#else
static int last_start_failed(void) { return bootguard.failed != 0; }
#endif

/* The second core, in a build that has it (fm1.toml cpu2 = true; FM1B_CPU2=0 in the build's environment makes
 * one without): the core's render hands it every other voice. Where it does not start, or once it has been given up, the
 * first core does all of them, as in a build without it. The sound is the same either way. */
#ifndef FM1B_CPU2
#define FM1B_CPU2 0
#endif
#define TWO_CORES FM1B_CPU2
#if TWO_CORES
static int core2_run(void (*fn)(uint32_t), uint32_t arg) { return plat_cpu2_on() && plat_cpu2_run(fn, arg); }
static int core2_wait(void) { return plat_cpu2_wait(); }
#endif

/* MIDI out, to the computer over USB (the FM-1's TRS jack is an input only): the core's notes and clock from the
 * render, its knobs from the main loop; plat_midi_send takes both. With nobody listening there is no room, and
 * nothing is queued. What may be left out when the queue is nearly full goes in tiers: midi_tiers.h.
 * (A one-byte real-time message, 0xF8 and up, packs as code index 0xF, which status >> 4 gives.) */
static void midi_send(uint8_t status, uint8_t d1, uint8_t d2)
{
    uint32_t pkt = (uint32_t)(status >> 4) | (uint32_t)status << 8 | (uint32_t)d1 << 16 | (uint32_t)d2 << 24;
    if (plat_midi_out_room(TO_USB) > midi_out_need(status, d1))
        plat_midi_send(pkt, TO_USB);
}

/* ---- the loop, kept through a power-off while it plays (#39) -----------------------------------
 * A store save erases a sector first, and an erase stops the sound for about 50 ms, so the store only ever
 * took the loop once it had been stopped: a loop left playing was lost at power-off. The loop has a ring of
 * its own instead, in the data area: RING_SLOTS slots of RING_SECT sectors each. A save is a record: a header
 * page (magic, a sequence number, the blob's length and its CRC-32) and the blob after it, page-aligned. A
 * slot takes records one after another until it is full, so a loop that goes on playing has room for many
 * saves (a light loop dozens, the fullest loop about four), not one a slot. A save goes into erased space, a
 * 256-byte page a frame (a page program is short, no erase), with the header written last, so a record whose
 * header reads right holds a whole loop. The newest such record is the loop. A save given up part-way gets a
 * header marked dead, so the records after it can still be found. Slots are erased at start (before the
 * sound) and once the loop is stopped and nothing sounds, all but the one holding the loop, a sector a frame.
 * Before this the loop was store object OBJ_LOOP: read once, when the ring holds none. */
#define RING_SECT 5u                     /* sectors a slot */
#define RING_SLOTS 4u
#define RING_SLOT_BYTES (RING_SECT * 4096u)
#define RING_BASE (0xDC000u - RING_SLOTS * RING_SLOT_BYTES)   /* 0xC8000: just under the panel's calibration */
#define RING_BODY 256u
#define RING_MAGIC 0x52323241u           /* "A22R": a record */
#define RING_DEAD 0x58323241u            /* "A22X": a save given up; its length says how far it got */
#define RING_PAGE 256u
#define RING_FULL RING_SLOT_BYTES        /* (a slot's fill: no more records go in until it is erased) */
typedef char loop_fits_a_slot[H1_LOOP_MAX <= RING_SLOT_BYTES - RING_BODY ? 1 : -1];
typedef char ring_in_the_data_area[RING_BASE >= PLAT_DATA_LO ? 1 : -1];

static struct {
    int cur;                             /* the slot that holds the loop, -1 none */
    uint32_t cur_at;                     /* where in it the loop's record begins */
    uint32_t seq;                        /* the newest record's sequence number */
    uint32_t fill[RING_SLOTS];           /* where a slot's next record goes; RING_FULL: none fits */
    int to;                              /* the slot being written, -1 none */
    uint32_t to_at;                      /* where in it the record begins */
    uint32_t len, at, crc;               /* what is being written: its length, how far, its CRC */
    uint32_t wait;                       /* frames the loop has been dirty: a save begins a moment after */
    uint32_t defer;                      /* frames this save has waited for lighter renders, in all (ring_go) */
    uint32_t gen;                        /* h1_loop_gen when it began: h1_loop is the engine's own memory, so a
                                          * take recorded meanwhile moves its events, and that save is dropped */
} ring = {-1, 0, 0, {RING_FULL, RING_FULL, RING_FULL, RING_FULL}, -1, 0, 0, 0, 0, 0, 0, 0};
/* After a failed start the ring is not read (its record might be what crashed it), so nothing is known of its
 * slots. The saved tape is then left alone until a new tape is saved over it: every slot erased first, since an
 * old record's higher number would otherwise win at the next start. (Once ring_step erased all four at once.) */
static int ring_held;
/* The slot being erased a sector a frame (ring_erase_some), -1 none, and its next sector */
static int er_slot = -1;
static uint32_t er_k;
#ifdef PLAT_HOST
static void ring_boot_state(void)
{
    int i;
    er_slot = -1;
    ring.cur = ring.to = -1;
    ring.seq = ring.wait = 0;
    for (i = 0; i < (int)RING_SLOTS; i++) ring.fill[i] = RING_FULL;
}
#endif
#define RING_WAIT 20u                    /* (a third of a second: the save keeps clear of the take's own close) */
#define RING_HEAVY 75u                   /* renders this % of their time and over: a write waits (ring_go) */
#define RING_DEFER 24u                   /* frames one save may wait in all for lighter renders (about a second) */
static const uint8_t *ring_src;          /* the blob being written, as h1_loop gave it: it holds until a take
                                          * records again (then the loop is dirty again, and saved anew) */
#define RING_MAX (RING_SLOT_BYTES - RING_BODY)
static uint8_t wpage[RING_PAGE];         /* a page on its way to the flash: the ring's or a project's (one a frame) */

static uint32_t ring_off(int slot) { return RING_BASE + (uint32_t)slot * RING_SLOT_BYTES; }
static uint32_t ring_pages(uint32_t n) { return (n + RING_PAGE - 1u) & ~(RING_PAGE - 1u); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static uint32_t crc32_run(uint32_t c, const uint8_t *p, uint32_t n)   /* c: ~0 to start, ~ of it at the end */
{
    uint32_t k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c;
}
static uint32_t crc32_of(const uint8_t *p, uint32_t n) { return ~crc32_run(0xFFFFFFFFu, p, n); }

static int ring_blank(const uint8_t *p, uint32_t n)
{
    while (n--)
        if (*p++ != 0xFFu)
            return 0;
    return 1;
}

/* a slot's records, read through: the newest whole one (its place, length and number, when *len is 0 or its
 * number is newer than *seq) and where the next record would go (RING_FULL when the rest is not erased:
 * a save cut off by a power-off, or another firmware's bytes) */
static uint32_t ring_scan(int slot, uint32_t *at, uint32_t *len, uint32_t *seq)
{
    const uint8_t *s = plat_data(ring_off(slot));
    uint32_t o = 0, m, n;
    if (!s)
        return RING_FULL;
    while (o + RING_BODY <= RING_SLOT_BYTES) {
        const uint8_t *h = s + o;
        m = rd32(h);
        if (m == 0xFFFFFFFFu)                        /* no header: the rest erased is room; else a cut-off save */
            return ring_blank(h, RING_SLOT_BYTES - o) ? o : RING_FULL;
        n = rd32(h + 8);
        if ((m != RING_MAGIC && m != RING_DEAD) || n > RING_MAX || o + RING_BODY + ring_pages(n) > RING_SLOT_BYTES)
            return RING_FULL;
        if (m == RING_MAGIC && n >= 12u && crc32_of(h + RING_BODY, n) == rd32(h + 12)
            && (*len == 0 || (int32_t)(rd32(h + 4) - *seq) > 0)) {
            *at = o;
            *len = n;
            *seq = rd32(h + 4);
        }
        o += RING_BODY + ring_pages(n);
    }
    return o;
}

static int ring_sector_erase(uint32_t off)      /* one 4 KiB sector, timed (ERASE; on the host: expect ring_erases) */
{
    uint32_t t0 = plat_ticks24();
    int rc;
#ifdef PLAT_HOST
    host_er_n++;
    host_er_frame++;
#endif
    rc = plat_data_erase(off);
    erase_us_frame += US(plat_ticks24() - t0);
    perf.erase_n++;
    return rc;
}

static void ring_erase(int slot)
{
    uint32_t k, at = 0, len = 0, seq = 0;
    for (k = 0; k < RING_SECT; k++)
        if (ring_sector_erase(ring_off(slot) + k * 4096u) != 0)
            break;
    ring.fill[slot] = ring_scan(slot, &at, &len, &seq);
}

/* at start, before the sound: the newest whole loop, or 0 */
static const uint8_t *ring_start(uint32_t *len)
{
    uint32_t at, n, s, best = 0;
    int i;
    er_slot = -1;                                /* (an erase cut off by the restart: done below, if anything is left) */
    ring.cur = -1;
    ring.to = -1;                                /* (a save cut off by the restart is not carried on) */
    ring.wait = 0;
    ring.seq = 0;
    for (i = 0; i < (int)RING_SLOTS; i++) {
        at = n = s = 0;
        ring.fill[i] = ring_scan(i, &at, &n, &s);
        if (n && (ring.cur < 0 || (int32_t)(s - ring.seq) > 0)) {
            ring.cur = i;
            ring.cur_at = at;
            ring.seq = s;
            best = n;
        }
    }
    for (i = 0; i < (int)RING_SLOTS; i++)        /* every other slot erased now, while it costs nothing */
        if (i != ring.cur && ring.fill[i] != 0)
            ring_erase(i);
    *len = best;
    return ring.cur >= 0 ? plat_data(ring_off(ring.cur) + ring.cur_at) + RING_BODY : 0;
}

/* a save given up part-way: its header says so, and how far it got, so the records after it can be found */
/* a write into the ring, timed for the PERF line (FLASH): a page program holds the interrupts off while it runs */
static int ring_write(uint32_t off, const void *src, uint32_t n)
{
    uint32_t t0 = plat_ticks24(), us;
    int rc = plat_data_write(off, src, n);
    us = US(plat_ticks24() - t0);
    perf.flash_n++;
    if (us > perf.flash_max)
        perf.flash_max = us;
    return rc;
}

static void ring_drop(void)
{
    static uint8_t head[16];
    uint32_t used = RING_BODY + ring_pages(ring.at);
    wr32(head, RING_DEAD);
    wr32(head + 4, 0);
    wr32(head + 8, ring.at);
    wr32(head + 12, 0);
    ring.fill[ring.to] = ring_write(ring_off(ring.to) + ring.to_at, head, sizeof head) == 0
        ? ring.to_at + used : RING_FULL;
    ring.to = -1;
}

/* Whether a write into the ring may go now. A page program holds the interrupts off while it runs (FLASH on the PERF
 * line), the audio's too: one that lands as a render is due delays that render, and a render near its limit then
 * goes out late. So a write is put off while the renders are heavy (RING_HEAVY), but a save waits RING_DEFER frames
 * in all, not a page: a jam is heavy all the time, and a save stretched over many seconds would be dropped by every
 * take meanwhile (h1_loop_gen) and leave the last one off the flash at a power-off. Then a write waits for a render to
 * have just finished, which leaves it the most time before the next, sending the panel's rectangles meanwhile.
 * (Not on the host: its renders do not come by themselves while the main loop waits.) The projects' saves go by it
 * too, with a count of their own. */
static int flash_go(uint32_t *defer)            /* (the ring's and the projects' saves: each its own count) */
{
    if (au_recent * 100u >= RING_HEAVY * pf_block_us && *defer < RING_DEFER) {
        (*defer)++;
        return 0;
    }
#ifndef PLAT_HOST
    {
        uint32_t seen = au_seq, t0 = plat_ticks24();
        while (au_seq == seen && US(plat_ticks24() - t0) < pf_block_us + 1000u)
            send_some();                         /* (a render a block: at most one block's wait) */
    }
#endif
    return 1;
}

/* A slot back to erased, a 4 KiB sector a frame; 1 once the slot is done. Each erase stalls the sound and the input
 * while it runs (about 17 ms by h1's count, up to 50 by this file's notes), and a whole slot's five in one frame were
 * a cut long enough to hear and to see in the LEDs, with a key struck meanwhile late (jakehandy/h1#58, #64). A slot
 * part erased is full until it is done, so no save goes into it. Sector 0, its first header, goes first: a power-off
 * part way leaves a slot whose scan stops at that blank header (no older record comes back from the sectors after
 * it), which ring_start erases. Only from ring_step, only in silence. */
static int ring_erase_some(int slot)
{
    uint32_t at = 0, len = 0, seq = 0;
    if (er_slot != slot) {
        er_slot = slot;
        er_k = 0;
        ring.fill[slot] = RING_FULL;
    }
    if (ring_sector_erase(ring_off(slot) + er_k * 4096u) == 0 && ++er_k < RING_SECT)
        return 0;
    ring.fill[slot] = ring_scan(slot, &at, &len, &seq);   /* (done, or an erase failed: the slot read again) */
    er_slot = -1;
    return 1;
}

/* every frame: a page of a save under way, or a save begun, or (stopped and silent) a sector of a slot erased */
static void ring_step(void)
{
    int i;
    if (ring_held) {
        if (!h1_loop_dirty() || !h1_quiet())
            return;                              /* nothing new to save (or not silent): the old tape stays */
        if (er_slot >= 0) {
            (void)ring_erase_some(er_slot);      /* a new tape to save over it: the slots erased first */
            return;
        }
        for (i = 0; i < (int)RING_SLOTS; i++)
            if (ring.fill[i] != 0) {
                (void)ring_erase_some(i);
                return;
            }
        ring_held = 0;
        ring.cur = -1;
        ring.seq = 0;
    }
    if (ring.to >= 0) {
        uint32_t off = ring_off(ring.to) + ring.to_at, n = ring.len - ring.at;
        if (!flash_go(&ring.defer))
            return;                              /* (the renders are heavy: this frame's page waits) */
        if (n) {
            uint8_t *page = wpage;               /* (a page copied out first: what is written is what the CRC
                                                  * covers, even where the engine's bytes move meanwhile) */
            uint32_t k;
            if (n > RING_PAGE)
                n = RING_PAGE;
            for (k = 0; k < n; k++)
                page[k] = ring_src[ring.at + k];
            ring.crc = crc32_run(ring.crc, page, n);
            if (ring_write(off + RING_BODY + ring.at, page, n) != 0) {
                ring.at += n;                    /* (a failed write: given up, the save tried anew) */
                ring_drop();
                return;
            }
            ring.at += n;
            if (h1_loop_gen() != ring.gen)
                ring_drop();                     /* (a take moved the events under it: given up, tried anew) */
        } else {
            static uint8_t head[16];
            wr32(head, RING_MAGIC);
            wr32(head + 4, ring.seq + 1u);
            wr32(head + 8, ring.len);
            wr32(head + 12, ~ring.crc);
            ring.fill[ring.to] = ring.to_at + RING_BODY + ring_pages(ring.len);
            if (ring_write(off, head, sizeof head) == 0) {
                const uint8_t *h = plat_data(off);
                if (h && rd32(h) == RING_MAGIC && rd32(h + 8) == ring.len
                    && crc32_of(h + RING_BODY, ring.len) == rd32(h + 12)) {
                    ring.cur = ring.to;
                    ring.cur_at = ring.to_at;
                    ring.seq++;
                    h1_loop_saved();
                }
            } else
                ring.fill[ring.to] = RING_FULL;
            ring.to = -1;
        }
        return;
    }
    if (!h1_loop_dirty())
        ring.wait = 0;
    else if (++ring.wait > RING_WAIT) {
        uint32_t size, gen = h1_loop_gen();
        const uint8_t *p = (const uint8_t *)h1_loop(&size);
        if (!size || size > RING_MAX)
            return;
        for (i = 0; i < (int)RING_SLOTS; i++) {  /* the slot holding the loop first, then the others */
            int sl = ring.cur >= 0 ? (ring.cur + i) % (int)RING_SLOTS : i;
            if (ring.fill[sl] + RING_BODY + ring_pages(size) <= RING_SLOT_BYTES) {
                ring_src = p;
                ring.to = sl;
                ring.defer = 0;                  /* (a new save: its own RING_DEFER) */
                ring.to_at = ring.fill[sl];
                ring.len = size;
                ring.gen = gen;
                ring.at = 0;
                ring.crc = 0xFFFFFFFFu;
                return;
            }
        }
    }
    if (!h1_quiet())
        return;                                  /* stopped and silent: a slot no loop is in back to erased, a sector */
    if (er_slot >= 0) {                          /* a frame (the sound stalls for each) */
        (void)ring_erase_some(er_slot);
        return;
    }
    for (i = 0; i < (int)RING_SLOTS; i++)
        if (ring.fill[i] != 0 && i != ring.cur && i != ring.to) {
            (void)ring_erase_some(i);
            return;
        }
}

#ifdef PLAT_HOST
static void ring_dump(void)
{
    printf("ring: loop in slot %d at 0x%X (0x%X), seq %u, fill %X %X %X %X\n", ring.cur, (unsigned)ring.cur_at,
           ring.cur >= 0 ? (unsigned)(ring_off(ring.cur) + ring.cur_at) : 0u, (unsigned)ring.seq,
           (unsigned)ring.fill[0], (unsigned)ring.fill[1], (unsigned)ring.fill[2], (unsigned)ring.fill[3]);
}
#endif

/* ---- projects: four tapes and their settings, kept apart from the one playing (docs/PLAN-projects.md) -------------
 * Five slots of five sectors just under the tape's ring, 0xAF000..0xC7FFF: four projects and a spare. 0x97000..0xAEFFF
 * stays free (a bigger app slot could end at 0xAF000). A project is a record as the ring's are: a header page, then
 * the state (h1_state) and the loop (h1_loop), each page-aligned. The header (magic, version, the project's number, a
 * sequence number, the two lengths, the body's CRC-32, and its own CRC-32) goes last, so a record whose header reads
 * right holds a whole project. A save goes into a free slot, erased first, and once its header is whole the slot that
 * held that project before is free: a power-off at any point leaves the old copy in charge. At start the newest whole
 * record of each project wins; anything else (erased, a torn save, another firmware's bytes) is a free slot, never
 * loaded, erased before a save goes in. The data area is shared: a firmware installed after A22 may write here. */
#define PROJ_N 4
#define PROJ_SLOTS 5
#define PROJ_SECT 5u
#define PROJ_SLOT_BYTES (PROJ_SECT * 4096u)
#define PROJ_BASE (RING_BASE - (uint32_t)PROJ_SLOTS * PROJ_SLOT_BYTES)   /* 0xAF000: just under the ring */
#define PROJ_MAGIC 0x50323241u           /* "A22P" */
#define PROJ_VERSION 1u
#define PROJ_HEAD 32u                    /* the header's bytes, at the start of its page (RING_BODY) */
#define PAGES(n) (((n) + RING_PAGE - 1u) & ~(RING_PAGE - 1u))
typedef char proj_in_the_data_area[PROJ_BASE == 0xAF000u && PROJ_BASE >= PLAT_DATA_LO ? 1 : -1];
typedef char proj_fits_a_slot[RING_BODY + PAGES(H1_STATE_MAX) + PAGES(H1_LOOP_MAX) <= PROJ_SLOT_BYTES ? 1 : -1];

static struct {
    int8_t of[PROJ_N];                   /* the slot holding project n, -1 none */
    uint8_t clean[PROJ_SLOTS];           /* 1: the slot is erased whole, and a save may go in */
    uint32_t seq;                        /* the newest record's sequence number */
    int want;                            /* a save asked for: the project, -1 none */
    int doing;                           /* the project being saved */
    int to;                              /* the slot it goes into, -1 none */
    int er;                              /* the slot being erased, a sector a frame, -1 none */
    uint32_t er_k;                       /* its next sector */
    uint32_t slen, llen;                 /* the save's state and loop, their bytes */
    uint32_t at, crc, gen, defer;        /* how far into the body, its CRC so far, h1_loop_gen at the start, waits */
    uint32_t saves, fails;               /* saves done and given up since the start */
    int load;                            /* a load asked for: the project, -1 none */
    uint32_t load_wait;                  /* frames since it silenced the sound (0: not yet) */
    int load_rc;                         /* the last load's: 0 taken, -1 no such project, -2 refused (recording, or the
                                          * record no longer checks), -3 its state refused */
    uint32_t loads;                      /* loads done since the start */
} proj = {{-1, -1, -1, -1}, {0}, 0, -1, 0, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0};

static uint32_t proj_off(int s) { return PROJ_BASE + (uint32_t)s * PROJ_SLOT_BYTES; }

static int proj_file_slot(void);
static int proj_free(int s)              /* no project in it, no save going into it, no file being written into it */
{
    int p;
    if (s == proj_file_slot())
        return 0;
    for (p = 0; p < PROJ_N; p++)
        if (proj.of[p] == s)
            return 0;
    return s != proj.to;
}

/* a slot's record: 1 when it is whole (the header and the body check), with its project and number */
static int proj_read(int s, uint32_t *p, uint32_t *seq)
{
    const uint8_t *h = plat_data(proj_off(s));
    uint32_t sl, ll, c;
    if (!h || rd32(h) != PROJ_MAGIC || rd32(h + 4) != PROJ_VERSION || crc32_of(h, PROJ_HEAD - 4u) != rd32(h + 28))
        return 0;
    *p = rd32(h + 8);
    *seq = rd32(h + 12);
    sl = rd32(h + 16);
    ll = rd32(h + 20);
    if (*p >= (uint32_t)PROJ_N || sl > H1_STATE_MAX || ll > H1_LOOP_MAX)
        return 0;
    c = crc32_run(0xFFFFFFFFu, h + RING_BODY, sl);
    c = crc32_run(c, h + RING_BODY + PAGES(sl), ll);
    return ~c == rd32(h + 24);
}

/* at start (before the sound, a failed start too: nothing is loaded from here): which slot holds which project */
static void proj_start(void)
{
    uint32_t p, seq, best[PROJ_N] = {0};
    int s, any = 0;
    proj.want = proj.to = proj.er = -1;
    proj.seq = 0;
    for (s = 0; s < PROJ_N; s++)
        proj.of[s] = -1;
    for (s = 0; s < PROJ_SLOTS; s++) {
        const uint8_t *d = plat_data(proj_off(s));
        proj.clean[s] = (uint8_t)(d && ring_blank(d, PROJ_SLOT_BYTES));
        if (!proj_read(s, &p, &seq))
            continue;
        if (!any || (int32_t)(seq - proj.seq) > 0)
            proj.seq = seq;
        any = 1;
        if (proj.of[p] < 0 || (int32_t)(seq - best[p]) > 0) {
            proj.of[p] = (int8_t)s;      /* (an older copy of the same project: a free slot, erased later) */
            best[p] = seq;
        }
    }
}

/* ---- the projects as files over USB (fm1-base's companion: app.h APP_FILES; docs/PLAN-projects.md P4) ---------
 * projectN is project N's record as its slot holds it: the header page (the header's 32 bytes, then erased bytes),
 * the state from byte 256 and the loop from the next page. A computer keeps it as a file whatever it holds; written
 * back to any of the four, it becomes that project. A write goes into a free slot (proj_free: no project in it, no
 * save going into it), its sectors erased as the data reaches them, the body written as it comes; the header is kept
 * in RAM and written last, at commit, once the file's lengths and its body's CRC-32 check, with this project's number
 * and the next sequence number in it. Until then the project is what it was: a write given up leaves a free slot,
 * erased before it is used. A delete marks the record's magic 0 in place (no erase), and the slot is free. */
static struct {
    int on;                              /* a write under way */
    uint32_t i;                          /* its project */
    int slot;                            /* the slot it goes into */
    uint32_t size, erased;               /* the file's bytes; the slot's bytes erased so far */
    uint8_t head[PROJ_HEAD];             /* the header as it came */
} fw = {0, 0, -1, 0, 0, {0}};

static uint32_t proj_size(int s)         /* a slot's record as a file: its bytes */
{
    const uint8_t *h = plat_data(proj_off(s));
    return h ? RING_BODY + PAGES(rd32(h + 16)) + rd32(h + 20) : 0u;
}

int app_file(uint32_t i, plat_file_info_t *f)
{
    static const char *const NAMES[PROJ_N] = {"project1", "project2", "project3", "project4"};
    if (i >= (uint32_t)PROJ_N)
        return -1;
    f->name = NAMES[i];
    f->kind = PLAT_FK_PROJECT;
    f->flags = PLAT_FF_READ | PLAT_FF_WRITE | PLAT_FF_DELETE;
    f->size = proj.of[i] >= 0 ? proj_size(proj.of[i]) : 0u;
    f->room = PROJ_SLOT_BYTES;
    return 0;
}

int app_file_read(uint32_t i, uint32_t off, uint8_t *dst, uint32_t n)
{
    int s = i < (uint32_t)PROJ_N ? proj.of[i] : -1;
    const uint8_t *p = s >= 0 ? plat_data(proj_off(s)) : 0;
    uint32_t k;
    if (!p || off > proj_size(s) || n > proj_size(s) - off)
        return -1;
    for (k = 0; k < n; k++)
        dst[k] = p[off + k];
    return 0;
}

int app_file_busy(void)
{
    return !h1_loop_quiet() || proj.to >= 0 || proj.want >= 0 || proj.load >= 0;
}

int app_file_begin(uint32_t i, uint32_t size)
{
    int s;
    fw.on = 0;
    if (i >= (uint32_t)PROJ_N || size < RING_BODY + 12u || size > PROJ_SLOT_BYTES)
        return -1;
    for (s = 0; s < PROJ_SLOTS && !(proj_free(s) && proj.clean[s]); s++)
        ;                                        /* (an erased free slot first, else any free one) */
    if (s == PROJ_SLOTS)
        for (s = 0; s < PROJ_SLOTS && !proj_free(s); s++)
            ;
    if (s == PROJ_SLOTS)
        return -1;
    if (proj.er == s)
        proj.er = -1;                            /* (its erase part-way: done here, as the data reaches it) */
    fw.erased = proj.clean[s] ? PROJ_SLOT_BYTES : 0u;
    proj.clean[s] = 0;
    fw.on = 1;
    fw.i = i;
    fw.slot = s;
    fw.size = size;
    for (s = 0; s < (int)PROJ_HEAD; s++)
        fw.head[s] = 0xFFu;
    return 0;
}

int app_file_write(uint32_t i, uint32_t off, const uint8_t *src, uint32_t n)
{
    uint32_t base = proj_off(fw.slot), k;
    if (!fw.on || i != fw.i || off > fw.size || n > fw.size - off)
        return -1;
    for (k = 0; k < n && off + k < PROJ_HEAD; k++)
        fw.head[off + k] = src[k];               /* (the header: kept for the commit) */
    k = off + n > RING_BODY ? (off < RING_BODY ? RING_BODY - off : 0u) : n;
    if (k >= n)
        return 0;                                /* (the rest of the header page stays erased) */
    off += k, src += k, n -= k;
    while (fw.erased < off + n) {                /* the sectors the data reaches, erased first (sliced) */
        if (ring_sector_erase(base + fw.erased) != 0)
            return -1;
        fw.erased += 4096u;
    }
    return ring_write(base + off, src, n);
}

int app_file_commit(uint32_t i, uint32_t size, uint32_t crc)
{
    const uint8_t *h = plat_data(proj_off(fw.slot));
    uint32_t sl = rd32(fw.head + 16), ll = rd32(fw.head + 20), c, p, seq;
    (void)crc;                                   /* (the companion checked the file's own CRC-32 as it came) */
    if (!fw.on || i != fw.i || size != fw.size || !h)
        return -1;
    fw.on = 0;
    if (rd32(fw.head) != PROJ_MAGIC || rd32(fw.head + 4) != PROJ_VERSION || sl > H1_STATE_MAX || ll > H1_LOOP_MAX ||
        size != RING_BODY + PAGES(sl) + ll)
        return -1;                               /* (not a project of this version: the slot is free) */
    c = crc32_run(0xFFFFFFFFu, h + RING_BODY, sl);
    c = crc32_run(c, h + RING_BODY + PAGES(sl), ll);
    if (~c != rd32(fw.head + 24))
        return -1;
    wr32(fw.head + 8, i);                        /* (this project now, whichever it was saved as) */
    wr32(fw.head + 12, proj.seq + 1u);
    wr32(fw.head + 28, crc32_of(fw.head, PROJ_HEAD - 4u));
    if (ring_write(proj_off(fw.slot), fw.head, PROJ_HEAD) != 0 || !proj_read(fw.slot, &p, &seq) || p != i)
        return -1;
    proj.of[i] = (int8_t)fw.slot;                /* (the slot it was in before is free now) */
    proj.seq++;
    h1_proj_done(3, (int)i, 1);
    return 0;
}

int app_file_delete(uint32_t i)
{
    static const uint8_t zero[4] = {0, 0, 0, 0};
    int s = i < (uint32_t)PROJ_N ? proj.of[i] : -1;
    if (s < 0)
        return 0;
    if (ring_write(proj_off(s), zero, sizeof zero) != 0)   /* (bits to 0 in place: the record no longer reads) */
        return -1;
    proj.of[i] = -1;
    return 0;
}
static int proj_file_slot(void) { return fw.on ? fw.slot : -1; }

/* a free slot back to erased, a sector a frame (the sliced erase: the sound goes on). Sector 0, its header, first */
static void proj_erase_some(int s)
{
    if (proj.er != s) {
        proj.er = s;
        proj.er_k = 0;
        proj.clean[s] = 0;
    }
    if (ring_sector_erase(proj_off(s) + proj.er_k * 4096u) == 0 && ++proj.er_k < PROJ_SECT)
        return;
    {                                            /* (done, or an erase failed: the slot read again) */
        const uint8_t *d = plat_data(proj_off(s));
        proj.clean[s] = (uint8_t)(d && ring_blank(d, PROJ_SLOT_BYTES));
    }
    proj.er = -1;
}

/* a page of the save under way, or its header once the body is in */
static void proj_write(void)
{
    uint32_t off = proj_off(proj.to), body = PAGES(proj.slen) + proj.llen, n = 0, k, p, seq;
    const uint8_t *src;
    if (h1_loop_gen() != proj.gen) {             /* a take moved the loop under it: the save again, from the start */
        proj.want = proj.want >= 0 ? proj.want : proj.doing;
        proj.to = -1;                            /* (the slot is free and not erased: erased before it is used) */
        return;
    }
    if (!flash_go(&proj.defer))
        return;
    if (proj.at < body) {
        if (proj.at < PAGES(proj.slen)) {        /* the state, then the loop from the next page */
            src = (const uint8_t *)h1_state(0) + proj.at;
            n = proj.slen - proj.at;
        } else {
            src = (const uint8_t *)h1_loop(0) + (proj.at - PAGES(proj.slen));
            n = body - proj.at;
        }
        if (n > RING_PAGE)
            n = RING_PAGE;
        for (k = 0; k < n; k++)
            wpage[k] = src[k];
        proj.crc = crc32_run(proj.crc, wpage, n);
        if (ring_write(off + RING_BODY + proj.at, wpage, n) != 0) {
            proj.fails++;                        /* (given up: the slot is free and erased before it is used) */
            proj.to = -1;
            h1_proj_done(1, proj.doing, 0);
            return;
        }
        proj.at = proj.at < PAGES(proj.slen) ? PAGES(proj.at + n) : proj.at + n;
        return;
    }
    wr32(wpage, PROJ_MAGIC);
    wr32(wpage + 4, PROJ_VERSION);
    wr32(wpage + 8, (uint32_t)proj.doing);
    wr32(wpage + 12, proj.seq + 1u);
    wr32(wpage + 16, proj.slen);
    wr32(wpage + 20, proj.llen);
    wr32(wpage + 24, ~proj.crc);
    wr32(wpage + 28, crc32_of(wpage, PROJ_HEAD - 4u));
    if (ring_write(off, wpage, PROJ_HEAD) == 0 && proj_read(proj.to, &p, &seq) && p == (uint32_t)proj.doing) {
        proj.of[proj.doing] = (int8_t)proj.to;   /* (the slot it was in before is free now) */
        proj.seq++;
        proj.saves++;
        h1_proj_done(1, proj.doing, 1);
    } else {
        proj.fails++;
        h1_proj_done(1, proj.doing, 0);
    }
    proj.to = -1;
}

/* A project in place of what plays (P2), the main loop's. It waits for a save under way (a load would change what it
 * writes), refuses while a take goes in, and reads the record again (a computer may have written the slot since). The
 * sound is silenced first, as hold HOME does (CC 123 out), and the load waits for the render to have taken that (the
 * load empties the engine's queue) and for the tails to go (PROJ_QUIET frames at most: then what is left is cut). The state and the loop are taken with the render locked out, as at a start: the render
 * reads them, and the engine starts again from them. Nothing is copied: the record is read in place. */
#define PROJ_QUIET 30u
#define PROJ_TAKEN 3u                    /* frames the silence has before the load, quiet or not */
static void proj_load_step(void)
{
    int p = proj.load, s = proj.of[p];
    uint32_t q, seq, sl, f;
    const uint8_t *h;
    if (proj.to >= 0 || proj.want >= 0)
        return;
    if (s < 0 || h1_loop_taking() || !proj_read(s, &q, &seq) || q != (uint32_t)p) {
        proj.load_rc = s < 0 ? -1 : -2;
        proj.load = -1;
        h1_proj_done(2, p, 0);
        return;
    }
    if (proj.load_wait++ == 0)
        h1_silence();
    if (proj.load_wait < PROJ_TAKEN || (!h1_quiet() && proj.load_wait < PROJ_QUIET))
        return;                                  /* (the render takes the silence first: the load empties its queue) */
    h = plat_data(proj_off(s));
    sl = rd32(h + 16);
    f = plat_lock();
    proj.load_rc = h1_project_load(h + RING_BODY, sl, h + RING_BODY + PAGES(sl), rd32(h + 20)) == 0 ? 0 : -3;
    plat_unlock(f);
    if (proj.load_rc == 0)
        proj.loads++;
    h1_proj_done(2, p, proj.load_rc == 0);
    proj.load = -1;
}

/* the panel's asks taken, and what it shows told (h1_proj_news: the keys lit with SAVE held) */
static void proj_panel(void)
{
    uint32_t have = 0;
    int p, k = h1_proj_take(&p);
    if (k == 1)
        proj.want = p;
    else if (k == 2 && proj.load < 0) {
        proj.load = p;
        proj.load_wait = 0;
    }
    for (k = 0; k < PROJ_N; k++)
        if (proj.of[k] >= 0)
            have |= 1u << k;
    h1_proj_news(have, proj.to >= 0 ? proj.doing : proj.want);
}

/* every frame, before the ring: 1 when the projects had the flash this frame (the ring waits). A save asked for
 * begins once no take is going and the ring is not part-way through one; it waits for a free slot to be erased first
 * if none is. Stopped and silent, the free slots go back to erased, a sector a frame, so the next save starts at once. */
static int proj_step(void)
{
    int s;
    if (proj.to >= 0) {
        proj_write();
        return 1;
    }
    if (proj.want >= 0 && ring.to < 0 && !h1_loop_taking()) {
        for (s = 0; s < PROJ_SLOTS; s++)
            if (proj_free(s) && proj.clean[s])
                break;
        if (s == PROJ_SLOTS) {                   /* none erased: one erased first (five frames) */
            for (s = 0; s < PROJ_SLOTS && !proj_free(s); s++)
                ;
            proj_erase_some(proj.er >= 0 ? proj.er : s);
            return 1;
        }
        proj.doing = proj.want;
        proj.want = -1;
        proj.to = s;
        proj.clean[s] = 0;
        h1_state(&proj.slen);
        (void)h1_loop(&proj.llen);
        proj.gen = h1_loop_gen();
        proj.at = proj.defer = 0;
        proj.crc = 0xFFFFFFFFu;
        proj_write();
        return 1;
    }
    if (h1_quiet() && ring.to < 0 && er_slot < 0) {
        if (proj.er >= 0) {
            proj_erase_some(proj.er);
            return 1;
        }
        for (s = 0; s < PROJ_SLOTS; s++)
            if (proj_free(s) && !proj.clean[s]) {
                proj_erase_some(s);
                return 1;
            }
    }
    return 0;
}

#ifdef PLAT_HOST
static void proj_dump(void)
{
    int p, s;
    printf("proj:");
    for (p = 0; p < PROJ_N; p++)
        printf(" P%d %d", p + 1, proj.of[p]);
    printf(", clean");
    for (s = 0; s < PROJ_SLOTS; s++)
        printf(" %d", proj.clean[s]);
    printf(", seq %u, saving %d into %d (%u B in), saves %u, fails %u\n", (unsigned)proj.seq,
           proj.to >= 0 ? proj.doing + 1 : 0, proj.to, (unsigned)proj.at, (unsigned)proj.saves, (unsigned)proj.fails);
}
/* expect proj.N SLOT (project N's slot, -1 none), proj_same N (its record holds the state and the loop as they are
 * now, byte for byte), proj_saves N */
static int proj_expect(const char *a, const char *b)
{
    long want = strtol(b, 0, 0);
    if (!strncmp(a, "proj.", 5)) {
        int p = atoi(a + 5) - 1, got = p >= 0 && p < PROJ_N ? proj.of[p] : -9;
        if (got != want) {
            printf("FAIL expect %s == %s, got %d\n", a, b, got);
            exit(1);
        }
        printf("  ok %s == %d\n", a, got);
        return 1;
    }
    if (!strcmp(a, "proj_loads") || !strcmp(a, "proj_load_rc")) {
        long got = a[9] == 's' ? (long)proj.loads : (long)proj.load_rc;
        if (got != want) {
            printf("FAIL expect %s == %s, got %ld\n", a, b, got);
            exit(1);
        }
        printf("  ok %s == %ld\n", a, got);
        return 1;
    }
    if (!strcmp(a, "proj_saves")) {
        if ((long)proj.saves != want) {
            printf("FAIL expect proj_saves == %s, got %u\n", b, (unsigned)proj.saves);
            exit(1);
        }
        printf("  ok proj_saves == %u\n", (unsigned)proj.saves);
        return 1;
    }
    if (!strcmp(a, "proj_same")) {
        int p = (int)want - 1, s = p >= 0 && p < PROJ_N ? proj.of[p] : -1;
        uint32_t sl, ll;
        const uint8_t *st = (const uint8_t *)h1_state(&sl), *lp = (const uint8_t *)h1_loop(&ll), *h;
        h = s >= 0 ? plat_data(proj_off(s)) : 0;
        if (!h || rd32(h + 16) != sl || rd32(h + 20) != ll || memcmp(h + RING_BODY, st, sl)
            || memcmp(h + RING_BODY + PAGES(sl), lp, ll)) {
            printf("FAIL expect proj_same %s: slot %d does not hold the state and loop as they are\n", b, s);
            exit(1);
        }
        printf("  ok proj_same %s (slot %d, state %u B, loop %u B)\n", b, s, (unsigned)sl, (unsigned)ll);
        return 1;
    }
    return 0;
}
static int proj_host_cmd(const char *cmd, const char *a, const char *b)
{
    if (!strcmp(cmd, "proj") && !strcmp(a, "save")) {   /* proj save N: save to project N (1..4), as SAVE + EDIT + key will */
        int p = atoi(b) - 1;
        if (p >= 0 && p < PROJ_N)
            proj.want = p;
        return 1;
    }
    if (!strcmp(cmd, "proj") && !strcmp(a, "load")) {   /* proj load N: load project N (1..4), as SAVE + key will */
        int p = atoi(b) - 1;
        if (p >= 0 && p < PROJ_N) {
            proj.load = p;
            proj.load_wait = 0;
        }
        return 1;
    }
    if (!strcmp(cmd, "proj") && !strcmp(a, "rm")) {   /* proj rm N: project N deleted, as the companion's delete */
        (void)app_file_delete((uint32_t)(atoi(b) - 1));
        return 1;
    }
    if (!strcmp(cmd, "proj")) {                  /* proj: which slot holds each project, and the slots erased */
        proj_dump();
        return 1;
    }
    if (!strcmp(cmd, "expect") && !strcmp(a, "toast")) {   /* expect toast WORDS_SO: the toast showing (_ a space) */
        const char *t = h1_toast_now();
        int i;
        for (i = 0; b[i] && t[i] && (t[i] == b[i] || (b[i] == '_' && t[i] == ' ')); i++)
            ;
        if (b[i] || t[i]) {
            printf("FAIL expect toast %s, got \"%s\"\n", b, t);
            exit(1);
        }
        printf("  ok toast \"%s\"\n", t);
        return 1;
    }
    if (!strcmp(cmd, "expect") && !strcmp(a, "key_leds")) {   /* expect key_leds MASK: the keys the core lights */
        uint32_t m = h1_key_leds(), want = (uint32_t)strtoul(b, 0, 0);
        if (m != want) {
            printf("FAIL expect key_leds == 0x%X, got 0x%X\n", (unsigned)want, (unsigned)m);
            exit(1);
        }
        printf("  ok key_leds == 0x%X\n", (unsigned)m);
        return 1;
    }
    return !strcmp(cmd, "expect") && proj_expect(a, b);
}
#endif

void app_init(void)
{
    static uint32_t blob[PLAT_STORE_MAX / 4];               /* words: the store fills it, the core reads it */
    uint32_t size;
    int n;
    h1_init(44100);
    h1_while_drawing(send_some);
    h1_midi_out(midi_send);
    h1_usb_host_fn(usb_host);
#if TWO_CORES
    if (plat_cpu2_set(1) == 0)                              /* (app_init is the one moment it can start) */
        h1_two_cores(core2_run, core2_wait);
#endif
#if PLAT_USB_CH >= 8
    h1_stems(plat_usb_ch, PLAT_USB_CH_STRIDE);              /* USB audio's channels 3..10: T1 to T4 */
#endif
    proj_start();                                           /* (nothing loaded from there: read on a failed start too) */
    if (last_start_failed()) {
        ring_held = 1;                           /* (the saved tape left alone: ring_step) */
        return;
    }
    ring_held = 0;
    h1_state(&size);
    n = size <= sizeof blob ? plat_store_load(OBJ_STATE, blob, sizeof blob) : -1;
    if (n > 0)
        h1_state_load(blob, (uint32_t)n);                   /* refused unless it is this version's, or one it knows */
    {                                                       /* then the loop (the state's load empties it) */
        uint32_t len;
        const uint8_t *lp = ring_start(&len);
        if (lp)
            h1_loop_load(lp, len);
        else if ((n = plat_store_load(OBJ_LOOP, blob, sizeof blob)) > 0)
            h1_loop_load(blob, (uint32_t)n);                /* (one saved before the ring: it goes there next) */
    }
    erase_us_frame = perf.erase_n = 0;                        /* (the start's erases, before the sound, are not a frame's) */
#ifdef PLAT_HOST
    host_er_frame = 0;
#endif
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float gain_now;
    int held = h1_global_volume();                          /* (the knob may be the instrument's just now) */
    float p = (float)held * 0.001f;                         /* (held: a position, 0..1000, as h1_master was given it) */
    /* master_q12 is already fm1-base's square of the knob's position (APP_GAIN), so a held position is squared
     * here first: the held volume is then the one the knob gave a moment ago. (It was not, and holding made
     * everything louder: #38.) */
    float m = held < 0 ? (float)master_q12 * (1.0f / 4096.0f) : p * p;
    float gain = m * m;                                     /* and the square of that: MASTER's curve on the FM-1 */
    uint32_t i, t0 = plat_ticks24(), us, xr = plat_xruns();
    static uint32_t xr_seen;
    /* the half before went out late (the platform counts them): two in a row and the core lets a voice go. (Not
     * fm1-base's APP_OVERLOAD: that comes at 90 % of the time, where nothing is late yet.) */
    h1_late(xr != xr_seen);
    xr_seen = xr;
#if PLAT_USB_CH >= 8
    uint32_t run = 0;
    int16_t *u = plat_usb_ch(0, &run);                      /* the stems, at MASTER too (NULL: nobody records) */
#endif
    h1_render(lr, n);                                       /* 24-bit, already limited */
    for (i = 0; i < n; i++) {
        gain_now += (gain - gain_now) * 0.004f;             /* ~5 ms: turning MASTER does not click */
        lr[2 * i] = (int32_t)((float)lr[2 * i] * gain_now);
        lr[2 * i + 1] = (int32_t)((float)lr[2 * i + 1] * gain_now);
#if PLAT_USB_CH >= 8
        if (u) {                                            /* (the same gain, frame for frame, as the mix) */
            int32_t g = (int32_t)(gain_now * 32768.0f), k;
            if (!run && !(u = plat_usb_ch(i, &run)))
                continue;
            for (k = 0; k < 8; k++)
                u[k] = (int16_t)(u[k] * g >> 15);
            u += PLAT_USB_CH_STRIDE;
            run--;
        }
#endif
    }
    us = US(plat_ticks24() - t0);
    au_recent = us > au_recent - au_recent / 8u ? us : au_recent - au_recent / 8u;
    au_seq++;
    pf_audio += us;
    pf_blocks++;
    if (us > pf_audio_max)
        pf_audio_max = us;
    pf_block_us = n * 1000000u / 44100u;
}

void app_panic(void) { h1_midi(0xB0, 123, 0); }

void app_splash(void) { lcd_fill(0, 0, 240, 240, 0); }

void app_ui_init(void) { lcd_fill(0, 0, 240, 240, 0); }

void app_frame(void)
{
    uint32_t t0, t1, t2;
    input();
    usb_news();
    h1_master((int)(plat_master() * 1000u / 4096u));        /* shown while it turns; the gain is app_render's */
    t0 = plat_ticks24();
    proj_panel();
    if (proj.load >= 0)
        proj_load_step();                                   /* (a project in place of what plays: before the frame) */
    h1_frame(plat_ms());                                    /* (and most of the last frame goes out meanwhile) */
    t1 = plat_ticks24();
    send_rest();                                            /* what is left of the last frame */
    palette_sync();
    h1_present_begin();                                     /* this frame, queued: it goes out during the next */
    t2 = plat_ticks24();
    sent.bytes += h1_present_bytes();
    sent.rects += (uint32_t)h1_present_rects();
    perf.frames++;
    perf.draw += US(t1 - t0);
    perf.send += US(t2 - t1);
    if (US(t1 - t0) > perf.draw_max) perf.draw_max = US(t1 - t0);
    if (US(t2 - t1) > perf.send_max) perf.send_max = US(t2 - t1);
    plat_glow(1);
    plat_leds(h1_button_leds(), h1_key_leds());
    speak();
    /* the state's save erases flash, which stalls the sound for a moment: only when it is silent (h1_quiet: nothing
     * playing, the tails gone), or at once after SAVE, playing or not (h1_state_asked: one try, so a failing write does
     * not stall the sound frame after frame). The loop goes into its ring a page a frame, playing or not (ring_step). */
    if (h1_state_dirty() && (h1_quiet() || h1_state_asked())) {
        uint32_t size;
        const void *p = h1_state(&size);
        if (plat_store_save(OBJ_STATE, p, size) == 0)
            h1_state_saved();
    } else if (!proj_step())                                /* (a project's save, or the ring: one a frame) */
        ring_step();
    if (erase_us_frame > perf.erase_max)                      /* (ERASE: the most erase time in one frame) */
        perf.erase_max = erase_us_frame;
    erase_us_frame = 0;
#ifdef PLAT_HOST
    if (host_er_frame > host_er_max)
        host_er_max = host_er_frame;
    host_er_frame = 0;
#endif
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

static char *say_num(char *d, char *end, uint32_t v)
{
    char b[10];
    int n = 0;
    do
        b[n++] = (char)('0' + v % 10u);
    while ((v /= 10u) != 0u && n < 10);
    while (n && d < end)
        *d++ = b[--n];
    *d = 0;
    return d;
}

static void say_perf(void)
{
    char t[241], *d, *end = t + sizeof t - 1;
    uint32_t f, ms = plat_ms() - perf.since_ms, n = perf.frames ? perf.frames : 1u, nb, au, am, stg[H1_NSTG], stg_total;
    int k;
    f = plat_lock();
    nb = pf_blocks; au = pf_audio; am = pf_audio_max;
    pf_blocks = pf_audio = pf_audio_max = 0;
    stg_total = h1_stages(stg);
    plat_unlock(f);
    d = say_cat(t, end, "PERF. FPS ");
    d = say_num(d, end, ms ? (perf.frames * 1000u + ms / 2u) / ms : 0u);
    d = say_cat(d, end, ". DRAW ");
    d = say_num(d, end, (perf.draw / n + 500u) / 1000u);
    d = say_cat(d, end, " MAX ");
    d = say_num(d, end, (perf.draw_max + 500u) / 1000u);
    d = say_cat(d, end, ". SEND ");
    d = say_num(d, end, (perf.send / n + 500u) / 1000u);
    d = say_cat(d, end, " MAX ");
    d = say_num(d, end, (perf.send_max + 500u) / 1000u);
    d = say_cat(d, end, ". AUDIO ");
    d = say_num(d, end, nb ? (au / nb) * 100u / pf_block_us : 0u);
    d = say_cat(d, end, " MAX ");
    d = say_num(d, end, am * 100u / pf_block_us);
    {   /* the halves the render ran late for, and the voices let go to catch up, since the last PERF */
        static uint32_t xr_was, shed_was;
        uint32_t xr = plat_xruns(), sh = h1_voices_shed();
        d = say_cat(d, end, " LATE ");
        d = say_num(d, end, xr - xr_was);
        d = say_cat(d, end, " SHED ");
        d = say_num(d, end, sh - shed_was);
        xr_was = xr;
        shed_was = sh;
    }
    d = say_cat(d, end, ". KB ");                           /* sent to the panel a frame, and in how many rectangles */
    d = say_num(d, end, (sent.bytes / n + 512u) / 1024u);
    d = say_cat(d, end, " RECTS ");
    d = say_num(d, end, (sent.rects + n / 2u) / n);
    sent.bytes = sent.rects = 0;
    if (plat_stack_left(PLAT_STK_MAIN) >= 0 && plat_stack_left(PLAT_STK_IRQ) >= 0) {
        d = say_cat(d, end, ". STACK ");                    /* bytes never used since the start */
        d = say_num(d, end, (uint32_t)plat_stack_left(PLAT_STK_MAIN));
        d = say_cat(d, end, " ");
        d = say_num(d, end, (uint32_t)plat_stack_left(PLAT_STK_IRQ));
    }
#if TWO_CORES
    /* the cores making the sound, then: with two, how deep the second's stack has gone; with one, why
     * (plat_cpu2_gave_up) */
    d = say_cat(d, end, plat_cpu2_on() ? ". CORES 2 " : ". CORES 1 ");
    d = say_num(d, end, plat_cpu2_on() ? plat_cpu2_stack() : (uint32_t)plat_cpu2_gave_up());
#endif
    if (stg_total) {                                        /* the slowest render, stage by stage (h1_stages) */
        d = say_cat(d, end, ". STAGE");
        for (k = 0; k < H1_NSTG; k++) {
            d = say_cat(d, end, " ");
            d = say_num(d, end, (US(stg[k]) * 100u + pf_block_us / 2u) / pf_block_us);
        }
    }
    d = say_cat(d, end, ". FLASH ");                        /* the tape's flash writes, and the longest (us) */
    d = say_num(d, end, perf.flash_n);
    d = say_cat(d, end, " ");
    d = say_num(d, end, perf.flash_max);
    d = say_cat(d, end, ". ERASE ");                        /* the ring's sector erases, and the most in a frame (us) */
    d = say_num(d, end, perf.erase_n);
    d = say_cat(d, end, " ");
    d = say_num(d, end, perf.erase_max);
    d = say_cat(d, end, ". HELD ");                         /* erases since the start that held the sound whole */
    say_num(d, end, plat_flash_held());
    ann_say(t, 0, ANN_NOW);
    perf.frames = perf.draw = perf.draw_max = perf.send = perf.send_max = perf.flash_n = perf.flash_max = 0;
    perf.erase_n = perf.erase_max = 0;
    perf.since_ms = plat_ms();
}

static void speak(void)
{
    static uint32_t fx_at;                                  /* when FX went down */
    static int fx_said, fx_was, punch_was, inst_was = -1, algo_was = -1, page_was = -1, lay_was = -1, ask_was = 0, val_was[H1_NENC];
    char t[96], *d, *end = t + sizeof t - 1;
    int inst = h1_instrument(), algo = h1_algorithm(), page = h1_page(), lay = h1_layers(), k, quiet = 0;
    int ask = h1_clear_all_asked(), answer = h1_clear_all_answer();   /* (taken every frame: it is once) */
    const char *said = h1_proj_said();
    if (ann_on()) {
        int where = ann_where();
        if (lay != lay_was && lay_was >= 0)                 /* SEQ and REC together */
            ann_say(lay ? "LAYERS ON" : "LAYERS OFF", 0, ANN_NOW);
        if (h1_fx_held()) {                                 /* FX held: the punch strip is up. Said once it is a
                                                             * hold (a tap opens the FX page and says only that), and
                                                             * not when a punch already named itself */
            if (!fx_was) { fx_at = plat_ms(); fx_said = 0; }
            if (h1_punch()) fx_said = 1;
            else if (!fx_said && plat_ms() - fx_at >= 400u) {
                ann_say("PUNCH", 0, ANN_NOW);
                fx_said = 1;
            }
        }
        if (h1_punch() && h1_punch() != punch_was)          /* a punch starts: its name */
            ann_say(h1_punch_name(h1_punch()), 0, ANN_NOW);
        if (ask && !ask_was)                                /* REC held: the whole tape, asked first */
            ann_say("CLEAR ALL? TAP REC", 0, ANN_NOW);
        if (answer)                                         /* and its answer (no strcmp: the device has no libc) */
            ann_say(answer == 2 ? "TAPE CLEARED" : "TAPE KEPT", 0, ANN_NOW);
        if (said[0])                                        /* a project saved, loaded, asked or refused */
            ann_say(said, 0, ANN_NOW);
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
                if (!name || !name[0]) continue;            /* an empty column */
                d = say_cat(d, end, ". ");
                d = say_cat(d, end, name);
            }
            ann_say(t, 0, quiet ? 0 : ANN_NOW);
            quiet = 1;
        }
        /* what changed is said in one go, SELECT's (a new subpage: its name) first, then the columns that
         * changed with it. ann_later keeps one thing, the newest, so they are put together here */
        d = t;
        t[0] = 0;
        for (k = 0; k < H1_NENC; k++) {
            const char *name = 0, *text = 0;
            int v = h1_control(k, 0, 0, &name, &text);
            if (v != val_was[k] && !quiet && k != H1_ENC_PRESET && k != H1_ENC_ALGO && name && name[0]) {
                if (d != t)
                    d = say_cat(d, end, ". ");
                d = say_cat(d, end, name);
                d = say_cat(d, end, " ");
                d = say_cat(d, end, text[0] == '-' && !text[1] ? "NONE" : text);   /* (the LANE page's dash) */
            }
            val_was[k] = v;
        }
        if (d != t)
            ann_later(t, ANN_NOW, 250u, plat_ms());
    }
    fx_was = h1_fx_held();
    punch_was = h1_punch();
    inst_was = inst;
    algo_was = algo;
    page_was = page;
    lay_was = lay;
    ask_was = ask;
    if (pf_ask) {
        pf_ask = 0;
        if (ann_on())
            say_perf();
    }
    ann_tick(plat_ms());
}

/* the platform's own short messages (an update starting, a save that failed): to the reader */
void app_say(const char *a, const char *b)
{
    if (ann_on())
        ann_say(a, b, ANN_NOW | ANN_KEEP);
}
