#pragma once
#include <stdbool.h>

/* Sets up the spinner sprite (graphical shop only, after gfx_init). */
void busy_init(void);

/* Shows the spinner while on (after a short delay) and hides it when off. */
void busy_set(bool on);
