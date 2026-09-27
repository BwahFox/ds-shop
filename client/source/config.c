#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void config_load(Config *config, const char *path) {
    strncpy(config->server, DEFAULT_SERVER, MAX_SERVER_LEN - 1);
    config->server[MAX_SERVER_LEN - 1] = '\0';
    config->port = DEFAULT_PORT;
    config->server2[0] = '\0';
    config->port2 = 0;
    config->using_backup = 0;
    config->ssid[0] = '\0';
    strncpy(config->download_path, DEFAULT_DL_PATH, MAX_PATH_LEN - 1);
    config->download_path[MAX_PATH_LEN - 1] = '\0';
    config->text_ui = 0;
    config->music = 1;
    config->music_volume = 70;

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        val[strcspn(val, "\r\n")] = '\0';

        if (strcmp(key, "server") == 0) {
            strncpy(config->server, val, MAX_SERVER_LEN - 1);
        } else if (strcmp(key, "port") == 0) {
            config->port = atoi(val);
        } else if (strcmp(key, "server2") == 0) {
            strncpy(config->server2, val, MAX_SERVER_LEN - 1);
        } else if (strcmp(key, "port2") == 0) {
            config->port2 = atoi(val);
        } else if (strcmp(key, "ssid") == 0) {
            strncpy(config->ssid, val, MAX_SSID_LEN - 1);
        } else if (strcmp(key, "ui") == 0) {
            config->text_ui = strcmp(val, "text") == 0;
        } else if (strcmp(key, "music") == 0) {
            config->music = atoi(val) != 0;
        } else if (strcmp(key, "music_volume") == 0) {
            int v = atoi(val);
            config->music_volume = v < 0 ? 0 : v > 100 ? 100 : v;
        } else if (strcmp(key, "download_path") == 0) {
            strncpy(config->download_path, val, MAX_PATH_LEN - 1);
        }
    }
    fclose(f);
    if (config->port2 <= 0) config->port2 = config->port;
}

void config_swap_servers(Config *config) {
    char tmp[MAX_SERVER_LEN];
    memcpy(tmp, config->server, sizeof(tmp));
    memcpy(config->server, config->server2, sizeof(tmp));
    memcpy(config->server2, tmp, sizeof(tmp));
    int p = config->port;
    config->port = config->port2;
    config->port2 = p;
    config->using_backup = !config->using_backup;
}

bool config_save(const Config *config, const char *path) {
    Config c = *config;
    if (c.using_backup) config_swap_servers(&c);

    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f,
        "# DS Shop client config (the shop's Settings screen rewrites this file)\n"
        "\n"
        "# The shop server, and an optional backup (e.g. on a travel hotspot).\n"
        "# The shop uses whichever one answers, trying the one on the DS's own\n"
        "# network first.\n"
        "server=%s\n"
        "port=%d\n"
        "server2=%s\n"
        "port2=%d\n"
        "\n"
        "# Shop music (needs /ds-shop/music.bin): music=0 turns it off; volume 0..100\n"
        "music=%d\n"
        "music_volume=%d\n"
        "\n"
        "# ui=text starts the text interface instead of the graphical shop\n"
        "ui=%s\n"
        "\n"
        "# Reserved (the DS uses its WFC connection settings)\n"
        "ssid=%s\n"
        "download_path=%s\n",
        c.server, c.port, c.server2, c.port2, c.music, c.music_volume,
        c.text_ui ? "text" : "graphical", c.ssid, c.download_path);
    return fclose(f) == 0;
}
