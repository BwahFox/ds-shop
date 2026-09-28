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
    app_poll();
    wifi_signal_indicator_tick();
}
