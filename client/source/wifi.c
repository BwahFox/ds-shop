#include "wifi.h"
#include "app.h"
#include <nds.h>
#include <dswifi9.h>

#define WIFI_TIMEOUT_FRAMES 600  /* 10 seconds at 60 fps */

bool wifi_connect(const Config *config) {
    (void)config;  /* ssid field reserved for future calico wlmgr support */

    if (!Wifi_InitDefault(WFC_CONNECT)) {
        return false;
    }

    int status;
    int timeout = 0;
    do {
        app_vblank();
        status = Wifi_AssocStatus();
        timeout++;
    } while (status != ASSOCSTATUS_ASSOCIATED &&
             status != ASSOCSTATUS_CANNOTCONNECT &&
             timeout < WIFI_TIMEOUT_FRAMES);

    return (status == ASSOCSTATUS_ASSOCIATED);
}

void wifi_disconnect(void) {
    Wifi_DisconnectAP();
}
