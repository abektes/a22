/* h1 on the FM-1: the app's half of fm1-base's contract (device/unity.c includes this, then app.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "h1"
#define APP_SPLASH                       /* no paper splash: the screen stays the bezel's black until the */
void app_splash(void);                   /* instrument comes up */
#define APP_SPLASH_RGB RGB(0, 0, 0)
/* the platform's own canvas (its update, crash and safe-mode screens draw text a strip at a time): a strip of
 * 48 rows is enough, and the pool needs the rest for the framebuffer and the delay lines */
#define APP_CANVAS_PX (240u * 48u)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms */

/* the store: the saved state, and the platform's panel calibration */
enum { OBJ_STATE, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
