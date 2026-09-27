#pragma once
#include <nds.h>
#include "catalog.h"
#include "config.h"
#include "category.h"

/* The text-console UI (the fallback): homepage menu -> submenus -> category
   browsing. Expects main.c's text-mode video setup. */
void text_ui_run(PrintConsole *top, PrintConsole *bot, const Config *config);
