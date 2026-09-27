#pragma once
#include <stdbool.h>

#define ANIM_PATH  "/ds-shop/anim.bin"    /* made by tools/make_anim.py */

/* Loads the DSi Shop's sprites from ANIM_PATH into sprite VRAM (graphical
 * shop only, after gfx_init). If the file is missing or bad, nothing changes
 * and the shop keeps its own drawn box and spinner. */
void anim_load(void);

/* The "please wait" icon, on the top screen (busy.c uses it when loaded). */
bool anim_has_wait(void);
void anim_wait_draw(int frame, bool shown);

/* The download animation on the bottom screen: the box appears, fills with
 * the download, and the four of them bring it data in turn. */
bool anim_has_dl(void);
void anim_dl_start(void);          /* a new download: the box appears       */
void anim_dl_progress(int pct);    /* 0..100                                 */
void anim_dl_finish(void);         /* done: the box closes up                */
bool anim_dl_finished(void);       /* true once the closing has played      */
void anim_dl_stop(void);           /* hide it                                */

/* Called every VBlank (from busy.c's interrupt handler). */
void anim_vblank(void);
