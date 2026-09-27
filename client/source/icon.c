#include "icon.h"
#include "catalog.h"
#include "http.h"
#include <stdio.h>

/* One page of icons, plus headroom for the HTTP headers http_get briefly
   buffers before shifting the body to the front. Aligned for dmaCopy. */
#define ICONS_BUF_SIZE  (PAGE_SIZE * ICON_BYTES + 2048)

static char icons_buf[ICONS_BUF_SIZE] __attribute__((aligned(4)));
static int  icons_count = 0;

int icons_fetch_page(const char *server, int port, const char *category,
                     const char *query, int page) {
#ifdef TEST_MODE
    (void)server; (void)port; (void)category; (void)query; (void)page;
    icons_count = 0;          /* no icons in the offline build */
    return 0;
#else
    char qarg[128] = "";
    if (query && query[0]) {
        char enc[96];
        url_encode(query, enc, sizeof(enc));
        snprintf(qarg, sizeof(qarg), "&q=%s", enc);
    }
    char path[224];
    snprintf(path, sizeof(path), "/icons.bin?cat=%s%s&page=%d&size=%d",
             category, qarg, page, PAGE_SIZE);
    HttpResponse resp;
    int len = http_get(server, port, path, icons_buf, sizeof(icons_buf), &resp);
    if (len < 0 || resp.status != 200) {
        icons_count = 0;
        return -1;
    }
    icons_count = len / ICON_BYTES;
    return icons_count;
#endif
}

int icons_available(void) {
    return icons_count;
}

const void *icon_tiles(int idx) {
    if (idx < 0 || idx >= icons_count) return 0;
    return icons_buf + (size_t)idx * ICON_BYTES;
}

const void *icon_palette(int idx) {
    if (idx < 0 || idx >= icons_count) return 0;
    return icons_buf + (size_t)idx * ICON_BYTES + ICON_TILE_BYTES;
}
