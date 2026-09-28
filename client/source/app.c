#include <nds.h>
#include <stdio.h>
#include <stdlib.h>
#include "app.h"
#include "music.h"
#include "wifi.h"
#include "ui.h"

static FILE *g_partial_file;
static const char *g_partial_path;

void app_set_partial(void *file, const char *path) {
    g_partial_file = file;
    g_partial_path = path;
}

static void app_exit(void) {
    music_stop();
    if (g_partial_file) {
        fclose(g_partial_file);
        remove(g_partial_path);      /* it would show up as a broken game */
    }
#ifndef TEST_MODE
    wifi_disconnect();
#endif
    exit(0);
}

void app_poll(void) {
    if (pmShouldReset()) app_exit();
}

void app_vblank(void) {
    swiWaitForVBlank();
    bool lid_closed = (keysCurrent() & KEY_LID) != 0;
    music_update_lid(lid_closed);
    if (!pmMainLoop()) app_exit();
    lid_closed = (keysCurrent() & KEY_LID) != 0;
    if (lid_closed && pmIsSleepAllowed()) pmEnterSleep();
    music_update_lid((keysCurrent() & KEY_LID) != 0);
    wifi_signal_indicator_tick();
}
