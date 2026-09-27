#pragma once
#include <stdbool.h>

#define CONFIG_PATH         "/ds-shop/config.ini"
#define MAX_SERVER_LEN      64
#define MAX_SSID_LEN        33
#define MAX_PATH_LEN        128
#define DEFAULT_PORT        8888
#define DEFAULT_SERVER      "10.42.0.1"
#define DEFAULT_DL_PATH     "/roms/nds"

typedef struct {
    char server[MAX_SERVER_LEN];
    int  port;
    char server2[MAX_SERVER_LEN];  /* backup server, e.g. a travel hotspot's; empty = none */
    int  port2;
    int  using_backup;             /* server/port and server2/port2 are swapped in
                                      memory while the backup is the one in use */
    char ssid[MAX_SSID_LEN];       /* empty = use WFC firmware slots */
    char download_path[MAX_PATH_LEN];
    int  text_ui;                  /* ui=text: use the text fallback UI */
    int  music;                    /* music=0 turns the shop music off */
    int  music_volume;             /* music_volume=0..100 (default 70) */
} Config;

void config_load(Config *config, const char *path);

/* Writes the settings back (as the user set them, whichever server is in
   use). Returns false if the file couldn't be written. */
bool config_save(const Config *config, const char *path);

/* Swaps the main and backup server (see using_backup). */
void config_swap_servers(Config *config);
