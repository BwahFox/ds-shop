#pragma once
#include <nds.h>
#include <stdbool.h>
#include "font.h"

/* Software 2D renderer for the graphical UI.
   Each screen has a 256x192 BGR555 backbuffer in main RAM; drawing functions
   write into it and gfx_present() DMAs it to that screen's 16-bit bitmap BG.
   Colors must have bit 15 set (use RGB()), or the pixel shows as transparent. */

#define SCR_TOP  0
#define SCR_BOT  1
#define SCR_W    256
#define SCR_H    192

#define RGB(r, g, b)  ((u16)(0x8000 | RGB15((r) >> 3, (g) >> 3, (b) >> 3)))

/* the UI palette: light DSi-Shop greys and blues */
#define C_WHITE       RGB(255, 255, 255)
#define C_BG          RGB(244, 246, 248)
#define C_TEXT        RGB( 40,  48,  64)
#define C_TEXT_DIM    RGB(120, 128, 140)
#define C_TEXT_ON     RGB(255, 255, 255)   /* text on an accent fill        */
#define C_ACCENT      RGB( 48, 184, 240)   /* DSi Shop blue                  */
#define C_ACCENT_DARK RGB( 24, 136, 208)
#define C_ACCENT_PALE RGB(214, 240, 252)
#define C_BORDER      RGB(196, 204, 214)
#define C_GREEN       RGB( 56, 176,  72)
#define C_RED         RGB(216,  64,  56)

/* Video setup for the graphical UI (both engines in MODE_5, BG3 = 16-bit bitmap). */
void gfx_init(void);

u16 *gfx_buffer(int scr);

/* Copy the backbuffer(s) to the screen. mask: 1 = top, 2 = bottom. */
void gfx_present(int mask);

void gfx_clear(int scr, u16 color);
void gfx_background(int scr, const u16 *image);          /* full 256x192 image  */
void gfx_bottom_background(void);                          /* the plain shop bottom */

void gfx_fill(int scr, int x, int y, int w, int h, u16 color);
/* fill with `alpha` 0..16 (16 = opaque) blended over what's there */
void gfx_fill_blend(int scr, int x, int y, int w, int h, u16 color, int alpha);
void gfx_hline(int scr, int x, int y, int w, u16 color);
void gfx_vline(int scr, int x, int y, int h, u16 color);
/* rounded rectangle; border == fill for no border */
void gfx_round_rect(int scr, int x, int y, int w, int h, int r, u16 fill, u16 border);
/* w x h image, pixels with bit 15 clear are transparent */
void gfx_image(int scr, int x, int y, int w, int h, const u16 *pixels);
/* NDS banner icon (32x32, 4bpp tiles + 16-color palette) at `scale` 1 or 2 */
void gfx_icon(int scr, int x, int y, const void *tiles, const void *palette, int scale);

int  gfx_text_width(const Font *f, const char *s);
/* draw text with its top-left at (x, y); returns the x after the last char */
int  gfx_text(int scr, const Font *f, int x, int y, const char *s, u16 color);
/* like gfx_text but cut with "..." to fit `maxw` pixels */
int  gfx_text_fit(int scr, const Font *f, int x, int y, int maxw, const char *s, u16 color);
void gfx_text_center(int scr, const Font *f, int cx, int y, const char *s, u16 color);
/* word-wrap into a box; returns the number of lines used */
int  gfx_text_wrap(int scr, const Font *f, int x, int y, int w, int max_lines,
                   const char *s, u16 color);
