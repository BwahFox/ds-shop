#pragma once
#include <stdbool.h>

/* Leaving the app when the system asks: a tap of the DSi's power button, or
 * L+R+START+SELECT (calico reports both through pmShouldReset()). Calico
 * powers the DSi off if the button is still held about a second later, so
 * the exit has to be quick: stop the music, drop any half-written download,
 * leave Wi-Fi, and return to the loader (TWiLight Menu++, or the flashcart's
 * menu). */

/* swiWaitForVBlank(), then exit if asked to. Use it for every frame wait. */
void app_vblank(void);

/* exit now if asked to (for loops that don't wait a frame, like downloads) */
void app_poll(void);

/* the file a download is writing, removed if the app exits mid-download
   (NULL when none; the FILE is closed first) */
void app_set_partial(void *file, const char *path);
