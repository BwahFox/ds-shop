#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "catalog.h"
#include "category.h"
#include "config.h"

/* The shop's data model, shared by the graphical UI (gui_*.c) and the text
   fallback (ui.c): the categories, the download queue, and the downloads. */

/* ---- categories ---- */
extern const Category CAT_POPULAR, CAT_DS, CAT_DSIWARE;
extern const Category CAT_NES, CAT_GB, CAT_GBC, CAT_GBA;
extern const Category CAT_THEME_DSI, CAT_THEME_3DS, CAT_THEME_R4, CAT_THEME_AK;

/* ---- download queue ----
   Holds full copies of titles (the catalog is paged, so a title queued on
   page 1 is gone from RAM by page 9), each with its category, so one queue can
   mix DS games, VC games and themes. */
#define QUEUE_MAX 256
typedef struct {
    Title           title;
    const Category *cat;
} QueueItem;

int              queue_count(void);
const QueueItem *queue_item(int i);
bool             queue_contains(const Title *t, const Category *cat);
void             queue_toggle(const Title *t, const Category *cat);   /* add or remove */

/* ---- downloads ----
   Progress callbacks see the whole item: for a theme, `done`/`total` cover all
   of its files. */
typedef struct {
    void (*begin)(const QueueItem *item, int n, int total);   /* before each item */
    void (*progress)(size_t done, size_t total);
    void (*end)(const QueueItem *item, bool ok);               /* after each item */
} ShopDownloadUI;

bool shop_download(const Config *config, const QueueItem *item,
                   void (*progress)(size_t done, size_t total));

/* Download every queued item in order. Successful ones leave the queue, failures
   stay (so running the queue again retries only those). */
void shop_run_queue(const Config *config, const ShopDownloadUI *ui, int *ok, int *failed);

/* ---- the server ----
   Picks between the main and backup server (config.h) by asking each for the
   DS catalog, the one on the DS's own network first, so a DS on a travel
   hotspot doesn't wait on the home server's address. Leaves the one that
   answered in config->server/port. False if neither answered. */
bool shop_pick_server(Config *config);

/* ---- misc ---- */
void     shop_stir(void);            /* call once per input frame: feeds shop_random */
unsigned shop_random(unsigned n);    /* 0..n-1 */
void     shop_format_size(size_t bytes, char *buf, int buf_len);
