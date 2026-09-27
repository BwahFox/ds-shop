#pragma once
#include <stdbool.h>
#include "config.h"

#define MUSIC_PATH  "/ds-shop/music.bin"   /* made by tools/make_music.py */

/* Loads MUSIC_PATH and starts it looping on one sound channel. Does nothing
 * if music is off in the config or the file is missing or unreadable. */
void music_start(const Config *config);

/* Mutes/unmutes (the SELECT button in the graphical shop). */
void music_toggle(void);

void music_stop(void);

/* Applies changed music settings (on/off, volume) right away. */
void music_apply(const Config *config);
