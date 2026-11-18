/* h1's one piece of hardware code outside felucca-bsp: has the panel taken all of the transfer it was
 * given? The BSP can only wait for the answer (fm1_lcd_wait), and h1 sends a frame while it draws the
 * next, so it must be able to ask without waiting. This is the same bit the BSP waits on.
 *
 * It is kept apart in app/hw (listed as vendored in fm1.toml), so that the build's check for register
 * access outside the BSP still holds everywhere else in the app. */
#ifndef H1_HW_PANEL_H
#define H1_HW_PANEL_H

static inline int hw_panel_done(void) { return (FM1_LCD_SPI_CON & 0x8000u) != 0u; }

#endif
