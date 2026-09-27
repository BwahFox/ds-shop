#pragma once

/* NDS banner icon: 32x32, 4bpp, served as 512 tile bytes + 32 palette bytes. */
#define ICON_BYTES       544
#define ICON_TILE_BYTES  512
#define ICON_PAL_BYTES   32

/* Fetch the icons for catalog page `page` (one /icons.bin?page request).
   Icons line up with the titles on that page. Returns the count loaded,
   or -1 on error (e.g. older server without the endpoint). */
int icons_fetch_page(const char *server, int port, const char *category,
                     const char *query, int page);

/* How many icons are currently loaded for the active page (0 if none). */
int icons_available(void);

/* Tile/palette data for icon `idx` within the active page, or NULL if out of range. */
const void *icon_tiles(int idx);     /* ICON_TILE_BYTES bytes */
const void *icon_palette(int idx);   /* ICON_PAL_BYTES bytes (16 colors) */
