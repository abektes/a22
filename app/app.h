/* h1 on the FM-1: the app's half of fm1-base's contract (device/unity.c includes this, then app.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "A22"
/* the companion's files (fm1.toml companion = true): project1.a22 to project4.a22, each a project's record as its slot holds
 * it (app.c: a header page, the state, the loop). Read in place; a write goes into a free slot and counts at commit,
 * once its header and body check; busy while the tape plays, a take goes in, or a project saves or loads */
#define APP_FILES
int app_file(uint32_t i, plat_file_info_t *f);
int app_file_read(uint32_t i, uint32_t off, uint8_t *dst, uint32_t n);
int app_file_begin(uint32_t i, uint32_t size);
int app_file_write(uint32_t i, uint32_t off, const uint8_t *src, uint32_t n);
int app_file_commit(uint32_t i, uint32_t size, uint32_t crc);
int app_file_delete(uint32_t i);
int app_file_busy(void);
#define APP_SPLASH                       /* no paper splash: the screen stays the bezel's black until the */
void app_splash(void);                   /* instrument comes up */
#define APP_SPLASH_RGB RGB(0, 0, 0)
/* the platform's own canvas (its update, crash and safe-mode screens draw text a strip at a time): a strip of
 * 48 rows is enough, and the pool needs the rest for the framebuffer and the delay lines */
#define APP_CANVAS_PX (240u * 48u)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms */
#define APP_USB_HOST_NAME                /* the USB host reads a keyboard's name, to show when it connects */
/* a 4 KiB erase (the store's saves, the tape's ring) in windows of about 1 ms with the interrupts and the sound on
 * between them (fm1-base device/main_fm1.c data_erase, felucca-bsp fl_erase4k_sliced), instead of one window of
 * about 18 ms with the sound silenced: SAVE and the ring's erase no longer leave an audio half unrendered. A flash
 * part that ignores the suspend is waited out in one window as before (plat_flash_held counts those: PERF's HELD).
 * The render never returns with the second core's job running (core/synth.c, core/fx.c wait for it), which the
 * windows need: the XIP is off in each */
#define FM1B_ERASE_SLICED 1

/* the store: the saved state, the platform's panel calibration and the loop recorder's loop. Each object is an
 * A/B pair of 4 KiB sectors, written by the platform's storage (CRC, a torn write leaves the copy before in
 * charge) through felucca-bsp's flash driver, which knows both chips the FM-1 has shipped with (P25Q80H and
 * UC25HQ80). The loop's pair, 0xDE000..0xDFFFF, is the top of the data area the platform gives apps
 * (0x97000..0xDFFFF): nowhere near the boot loader, the app, the panel's calibration (0xDC000) or the
 * update's staging (0xE0000 up). The numbers are written in the records: never renumber them. */
enum { OBJ_STATE, OBJ_PANEL, OBJ_LOOP, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u}

#ifdef PLAT_HOST                         /* the host simulator's script: "badstart" (app.c) */
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
#endif

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
