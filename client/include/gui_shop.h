#pragma once
#include "config.h"

/* The graphical shop. Expects gfx_init() to have set up the video. */
void gui_run(Config *config);

/* http.c's waiting hook: called each frame a request goes without data. */
void gui_net_waiting(int frames);
