#include <nds.h>
#include <fat.h>
#include <dswifi9.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "config.h"
#include "wifi.h"
#include "catalog.h"
#include "shop.h"
#include "ui.h"
#include "gfx.h"
#include "gui.h"
#include "gui_shop.h"
#include "music.h"
#include "busy.h"
#include "anim.h"
#include "http.h"
#include "bg_top.h"
#include "bg_bot.h"

/* Boot: pick the interface, bring up the SD card, WiFi and the server, then
   hand off to the graphical shop (default) or the text fallback.
   TEST_MODE builds skip the SD card and network and use a fake catalog. */

#define STEP_SD       0
#define STEP_WIFI     1
#define STEP_SERVER   2

#define BOOT_OK         0
#define BOOT_NO_SD      1
#define BOOT_NO_WIFI    2
#define BOOT_NO_SERVER  3

/* Connect to WiFi and check the server; step() is told what's happening. */
static int boot_network(Config *config, void (*step)(int)) {
#ifdef TEST_MODE
    (void)config; (void)step;
    return BOOT_OK;
#else
    step(STEP_WIFI);
    if (!wifi_connect(config)) return BOOT_NO_WIFI;
    step(STEP_SERVER);
    if (!shop_pick_server(config)) {
        wifi_disconnect();
        return BOOT_NO_SERVER;
    }
    return BOOT_OK;
#endif
}

static void wait_for_button(void) {
    while (1) {
        app_vblank();
        scanKeys();
        if (keysDown() & (KEY_A | KEY_START)) return;
    }
}

/* ---- the text fallback ---- */

static PrintConsole g_top, g_bot;

static void text_step(int step) {
    consoleSelect(&g_bot);
    if (step == STEP_WIFI)   iprintf("Connecting to WiFi...\n");
    if (step == STEP_SERVER) iprintf("Contacting server...\n");
}

static int main_text(bool sd_ok, Config *config) {
    /* Main (top) engine: MODE_5 lets a text console (BG0) coexist with an
       extended 16-bit bitmap background (BG3). VRAM layout:
         A = main BG  (top console)
         B = main OBJ (icon sprite, set up in ui.c)
         C = sub  BG  (bottom console)
         D = main BG  (top background bitmap, slot 3 @ 0x06060000) */
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    vramSetBankD(VRAM_D_MAIN_BG_0x06060000);

    consoleInit(&g_top, 0, BgType_Text4bpp, BgSize_T_256x256, 0, 1, true,  true);
    consoleInit(&g_bot, 0, BgType_Text4bpp, BgSize_T_256x256, 8, 0, false, true);

    /* Top background: 16-bit bitmap (BG3, VRAM_D) behind the console. */
    int bg = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 24, 0);  /* 24*16KB = VRAM_D */
    bgSetPriority(bg, 3);
    bgSetPriority(0, 0);   /* top console in front */
    dmaCopy(bg_top, bgGetGfxPtr(bg), BG_TOP_W * BG_TOP_H * 2);

    /* Bottom background: 8bpp bitmap (BG3 sub, base 4 = upper VRAM_C) behind the
       console. Its palette puts bg colors in slots the font doesn't use, and
       dark text colors in the reserved slots. */
    int bbg = bgInitSub(3, BgType_Bmp8, BgSize_B8_256x256, 4, 0);
    bgSetPriority(bbg, 3);
    bgSetPriority(4, 0);   /* sub layer 0 (console) in front */
    dmaCopy(bg_bot,     bgGetGfxPtr(bbg), BG_BOT_W * BG_BOT_H);
    dmaCopy(bg_bot_pal, BG_PALETTE_SUB,   256 * 2);

    /* Top screen: dark text so it reads over the light DSi background. */
    BG_PALETTE[15]  = RGB15( 2,  3,  6);   /* slot 0  default      */
    BG_PALETTE[31]  = RGB15(18,  2,  2);   /* slot 1  red          */
    BG_PALETTE[47]  = RGB15( 0, 11,  2);   /* slot 2  green        */
    BG_PALETTE[111] = RGB15( 2,  7, 16);   /* slot 6  hint/cyan    */
    BG_PALETTE[127] = RGB15( 4,  5,  8);   /* slot 7  reset/dim    */
    BG_PALETTE[191] = RGB15( 2,  6, 22);   /* slot 11 select       */
    BG_PALETTE[255] = RGB15( 1,  2,  8);   /* slot 15 title        */

    consoleSelect(&g_bot);
    iprintf("\x1b[2J");
    iprintf("\x1b[0;0H\x1b[1;37m===[ DS SHOP ]=== \x1b[37m\n\n");

    if (!sd_ok) {
        iprintf("\x1b[31mFAT init failed!\x1b[37m\n");
        iprintf("Insert SD card and restart.\n");
        while (1) app_vblank();
    }
    iprintf("SD card OK\n");
    iprintf("Server: %s:%d\n\n", config->server, config->port);

    int r = boot_network(config, text_step);
    if (r == BOOT_NO_WIFI) {
        iprintf("\x1b[31mWiFi connection failed!\x1b[37m\n\n");
        iprintf("Configure WFC slot 1 via any\nWFC-enabled DS game.\n");
        iprintf("\nPress A to exit.\n");
        wait_for_button();
        return 1;
    }
    if (r == BOOT_NO_SERVER) {
        iprintf("\x1b[31mServer not reachable!\x1b[37m\n");
        iprintf("Is the server running at\n%s:%d ?\n", config->server, config->port);
        iprintf("\nPress A to exit.\n");
        wait_for_button();
        return 1;
    }

    text_ui_run(&g_top, &g_bot, config);
    return 0;
}

/* ---- the graphical shop ---- */

static void gui_step(int step) {
    if (step == STEP_WIFI)   gui_status("Connecting...", "Connecting to the Internet.", NULL);
    if (step == STEP_SERVER) gui_status("Connecting...", "Contacting the shop server.", NULL);
}

static int main_gui(bool sd_ok, Config *config) {
    gfx_init();
    busy_init();
    if (sd_ok) anim_load();          /* the DSi Shop's sprites, if they're there */
    http_set_hooks(busy_set, gui_net_waiting);

    if (!sd_ok) {
        gui_status("No SD card", "The SD card couldn't be read.", "Insert it and restart.");
        while (1) app_vblank();
    }

    int r = boot_network(config, gui_step);
    if (r == BOOT_NO_WIFI) {
        gui_message("Couldn't connect", "Set up a connection (WFC slot 1)",
                    "with any Wi-Fi-enabled DS game.");
        return 1;
    }
    if (r == BOOT_NO_SERVER) {
        char where[96];
        snprintf(where, sizeof(where), "Is it running at %s:%d?", config->server, config->port);
        gui_message("Server not reachable", where, NULL);
        return 1;
    }

    gui_run(config);
    return 0;
}

int main(void) {
    irqSet(IRQ_VBLANK, NULL);

    /* holding SELECT at boot picks the text fallback */
    app_vblank();
    scanKeys();
    bool text = (keysHeld() & KEY_SELECT) != 0;

    static Config config;
#ifdef TEST_MODE
    bool sd_ok = true;
    memset(&config, 0, sizeof(config));
    strncpy(config.server, "127.0.0.1", MAX_SERVER_LEN - 1);
    config.port = 8888;
#else
    bool sd_ok = fatInitDefault();
    config_load(&config, CONFIG_PATH);      /* defaults if the file is missing */
#endif
    if (config.text_ui) text = true;
#ifndef TEST_MODE
    if (sd_ok) music_start(&config);
#endif

    int r = text ? main_text(sd_ok, &config) : main_gui(sd_ok, &config);
#ifndef TEST_MODE
    wifi_disconnect();
#endif
    music_stop();
    return r;
}
