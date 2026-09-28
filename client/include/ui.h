#pragma once
#include <nds.h>
#include "catalog.h"
#include "config.h"
#include "category.h"

/* The text-console UI (the fallback): homepage menu -> submenus -> category
   browsing. Expects main.c's text-mode video setup. */
void text_ui_run(PrintConsole *top, PrintConsole *bot, const Config *config);

/* Draw the top-screen WiFi signal indicator (hardware sprite) in the top-right
   corner. The sprite is hidden if no connection is active. */
void draw_wifi_signal_indicator(void);
void wifi_signal_indicator_tick(void);