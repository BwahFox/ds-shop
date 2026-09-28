#include "ui.h"
#include "app.h"
#include "shop.h"
#include "icon.h"
#include <nds.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SCREEN_COLS     32   /* console width; PAGE_SIZE comes from catalog.h */

/* ---- color helpers ---- */
/* NOTE: \x1b[0m is a no-op in this libnds — it doesn't reset fontCurPal.
   Always terminate colored text with an explicit color to avoid bleed. */
#define COL_RESET   "\x1b[37m"     /* "reset" = explicit white (slot 7)  */
#define COL_TITLE   "\x1b[1;37m"   /* bold white  (slot 15) */
#define COL_SELECT  "\x1b[1;33m"   /* bold yellow (slot 11) */
#define COL_DIM     "\x1b[37m"     /* white       (slot 7)  */
#define COL_HINT    "\x1b[36m"     /* cyan        (slot 6)  */
#define COL_GREEN   "\x1b[32m"     /* green       (slot 2)  */
#define COL_RED     "\x1b[31m"     /* red         (slot 1)  */
#define COL_CYAN    "\x1b[36m"     /* cyan        (slot 6)  */

/* ---- progress state passed via global (only one download at a time) ---- */
static PrintConsole *g_progress_screen;

/* ---- top-screen game icon (hardware sprite on the main engine) ---- */
#define ICON_SPRITE_ID  0
#define ICON_X          112      /* centered horizontally: (256-32)/2 */
#define ICON_Y          32

static u16 *g_icon_gfx = NULL;

static void icon_setup(void) {
    vramSetBankB(VRAM_B_MAIN_SPRITE);
    oamInit(&oamMain, SpriteMapping_1D_32, false);
    g_icon_gfx = oamAllocateGfx(&oamMain, SpriteSize_32x32, SpriteColorFormat_16Color);
    oamClearSprite(&oamMain, ICON_SPRITE_ID);
    oamUpdate(&oamMain);
}

static void icon_hide(void) {
    oamClearSprite(&oamMain, ICON_SPRITE_ID);
    oamUpdate(&oamMain);
}

/* Show the icon for catalog entry `idx`, or hide the sprite if unavailable. */
static void icon_show(int idx) {
    const void *tiles = icon_tiles(idx);
    const void *pal   = icon_palette(idx);

    if (!tiles || !pal || !g_icon_gfx) {
        oamClearSprite(&oamMain, ICON_SPRITE_ID);
        oamUpdate(&oamMain);
        return;
    }

    dmaCopy(pal,   SPRITE_PALETTE, ICON_PAL_BYTES);   /* 16 colors */
    dmaCopy(tiles, g_icon_gfx,     ICON_TILE_BYTES);  /* 32x32 4bpp tiles */

    oamSet(&oamMain, ICON_SPRITE_ID,
           ICON_X, ICON_Y,
           0,                /* priority */
           0,                /* palette block 0 */
           SpriteSize_32x32, SpriteColorFormat_16Color,
           g_icon_gfx,
           -1, false, false, false, false, false);
    oamUpdate(&oamMain);
}

static void progress_cb(size_t received, size_t total) {
    consoleSelect(g_progress_screen);

    if (total > 0) {
        /* 64-bit: size_t is 32 bits on the ARM9, so received*100 wraps past
           ~43 MB and the percentage runs wild on big ROMs. */
        int pct     = (int)((uint64_t)received * 100 / total);
        if (pct > 100) pct = 100;
        int bar_len = 28;
        int filled  = pct * bar_len / 100;

        char bar[32];
        for (int i = 0; i < bar_len; i++)
            bar[i] = (i < filled) ? '=' : '-';
        bar[bar_len] = '\0';

        char rx_str[16], tot_str[16];
        shop_format_size(received, rx_str, sizeof(rx_str));
        shop_format_size(total,    tot_str, sizeof(tot_str));

        iprintf("\x1b[7;0H" COL_GREEN "[%s]" COL_RESET, bar);
        iprintf("\x1b[8;0H  %3d%%  %s / %s   ", pct, rx_str, tot_str);
    } else {
        char rx_str[16];
        shop_format_size(received, rx_str, sizeof(rx_str));
        iprintf("\x1b[7;0H" COL_GREEN "Received: %s   " COL_RESET, rx_str);
    }

    app_vblank();
}

/* ---- bottom screen: paged game list ----
   `page` holds only the current page; `sel_row` is the highlighted row in it. */
static void draw_list(PrintConsole *bot, const Catalog *page, const Category *cat,
                      const char *query, int sel_row, int page_num, int total_pages) {
    consoleSelect(bot);
    iprintf("\x1b[2J");

    char header[24];
    if (query && query[0])
        snprintf(header, sizeof(header), "Search:%s", query);
    else
        snprintf(header, sizeof(header), "%s", cat->name);
    iprintf("\x1b[0;0H" COL_TITLE "%-20.20s" COL_RESET
            COL_HINT " %d/%d" COL_RESET, header, page_num + 1, total_pages);

    for (int row = 0; row < PAGE_SIZE; row++) {
        iprintf("\x1b[%d;0H", row + 2);

        if (row >= page->count) {
            iprintf("%-32s", "");
            continue;
        }

        const char *name = page->titles[row].name;
        bool queued = queue_contains(&page->titles[row], cat);
        char qm = queued ? '*' : ' ';

        if (row == sel_row) {
            iprintf(COL_SELECT ">%c %-28.28s" COL_RESET, qm, name);
        } else if (queued) {
            iprintf(COL_GREEN " %c " COL_RESET "%-28.28s" COL_RESET, qm, name);
        } else {
            iprintf(COL_RESET "   %-28.28s" COL_RESET, name);
        }
    }

    iprintf("\x1b[21;0H" COL_DIM "----------------------------" COL_RESET);
    iprintf("\x1b[22;0H" COL_HINT "[A]Queue [X]Get(%d) [Y]Find " COL_RESET,
            queue_count());
    iprintf("\x1b[23;0H" COL_HINT "[L/R]Pg [B]Back [START]Exit" COL_RESET);
}

/* ---- top screen: selected game details ---- */
static void draw_detail(PrintConsole *top, const Title *t,
                        bool in_queue, int qcount) {
    consoleSelect(top);
    iprintf("\x1b[2J");

    iprintf("\x1b[0;0H" COL_TITLE "%-32.32s" COL_RESET, t->name);
    iprintf("\x1b[1;0H" COL_DIM "--------------------------------" COL_RESET);

    char desc_copy[MAX_DESC_LEN];
    strncpy(desc_copy, t->desc, MAX_DESC_LEN - 1);
    desc_copy[MAX_DESC_LEN - 1] = '\0';

    int row = 3;
    char *p = desc_copy;
    while (*p && row < 9) {
        char line[SCREEN_COLS + 1];
        int n = 0;
        while (*p && n < SCREEN_COLS) line[n++] = *p++;
        line[n] = '\0';
        iprintf("\x1b[%d;0H%-32s", row++, line);
    }

    if (t->size > 0) {
        char sz[16];
        shop_format_size(t->size, sz, sizeof(sz));
        iprintf("\x1b[10;0H" COL_HINT "Size: %s" COL_RESET "              ", sz);
    }

    iprintf("\x1b[12;0H%s", in_queue
            ? COL_GREEN "* In download queue " COL_RESET
            : COL_DIM   "  Not in queue      " COL_RESET);
    iprintf("\x1b[13;0H" COL_HINT "Queue: %d game%s        " COL_RESET,
            qcount, qcount == 1 ? "" : "s");

    iprintf("\x1b[15;0H" COL_SELECT "[A] Queue   [X] Download" COL_RESET);
}

/* ---- top-screen wifi signal indicator ---- */
void draw_wifi_signal_indicator(void) {
    u16 *bg = (u16 *)bgGetGfxPtr(3);
    if (!bg) return;

    const int icon_x = 230;
    const int icon_y = 154;
    const u16 black = RGB5(0, 0, 0) | 0x8000;
    const u16 green = RGB5(0, 31, 0) | 0x8000;
    const u16 gray = RGB5(17, 17, 17) | 0x8000;

    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            bg[(icon_y + y) * 256 + icon_x + x] = black;

    for (int x = 1; x < 15; x++) {
        bg[(icon_y + 1) * 256 + icon_x + x] = green;
        bg[(icon_y + 14) * 256 + icon_x + x] = green;
    }

    /* Hollow 3x3 antenna head, with a five-pixel stem. */
    for (int y = 5; y <= 7; y++)
        for (int x = 2; x <= 4; x++)
            bg[(icon_y + y) * 256 + icon_x + x] = gray;
    bg[(icon_y + 6) * 256 + icon_x + 3] = black;
    for (int y = 8; y <= 12; y++)
        bg[(icon_y + y) * 256 + icon_x + 3] = gray;

    static const int bar_x[] = { 5, 8, 11 };
    static const int bar_h[] = { 2, 5, 8 };
    for (int i = 0; i < 3; i++) {
        for (int y = 13 - bar_h[i]; y <= 12; y++) {
            for (int x = bar_x[i]; x < bar_x[i] + 2; x++)
                bg[(icon_y + y) * 256 + icon_x + x] = gray;
        }
    }
}

/* ---- top screen during a download: just the title + total size ---- */
static void draw_dl_top(PrintConsole *top, const Title *t) {
    consoleSelect(top);
    iprintf("\x1b[2J");

    iprintf("\x1b[4;0H" COL_TITLE "%-32.32s" COL_RESET, t->name);
    iprintf("\x1b[6;0H" COL_DIM "--------------------------------" COL_RESET);
    char sz[16];
    shop_format_size(t->size, sz, sizeof(sz));
    iprintf("\x1b[8;0H" COL_HINT "Total size: %s" COL_RESET, sz);
}

/* ---- bottom screen during a download: header + (bar drawn by progress_cb) ---- */
static void draw_dl_bottom(PrintConsole *bot, const Title *t, int n, int total) {
    consoleSelect(bot);
    iprintf("\x1b[2J");

    if (total > 1)
        iprintf("\x1b[2;0H" COL_TITLE "Downloading %d/%d..." COL_RESET, n, total);
    else
        iprintf("\x1b[2;0H" COL_TITLE "Downloading..." COL_RESET);
    iprintf("\x1b[3;0H" COL_DIM "----------------------------" COL_RESET);
    iprintf("\x1b[5;0H%-28.28s", t->name);
    iprintf("\x1b[9;0H" COL_DIM "Please wait..." COL_RESET);
}

static void draw_queue_summary(PrintConsole *top, int ok, int fail) {
    consoleSelect(top);
    iprintf("\x1b[2J");

    if (fail == 0)
        iprintf("\x1b[2;0H" COL_GREEN "Queue complete!" COL_RESET);
    else
        iprintf("\x1b[2;0H" COL_RED "Queue finished w/ errors" COL_RESET);
    iprintf("\x1b[3;0H" COL_DIM "--------------------------------" COL_RESET);

    iprintf("\x1b[5;0H" COL_GREEN "Downloaded: %d" COL_RESET, ok);
    if (fail > 0)
        iprintf("\x1b[6;0H" COL_RED "Failed:     %d" COL_RESET, fail);
    iprintf("\x1b[8;0H" COL_HINT "Saved to your SD card." COL_RESET);
}

/* The queue runner's callbacks draw the text download screens. */
static PrintConsole *g_dl_top, *g_dl_bot;

static void text_dl_begin(const QueueItem *item, int n, int total) {
    draw_dl_top(g_dl_top, &item->title);
    draw_dl_bottom(g_dl_bot, &item->title, n, total);
    g_progress_screen = g_dl_bot;          /* progress bar renders on the bottom */
}

/* Download every queued title; successful ones leave the queue, failures stay. */
static void run_queue(PrintConsole *top, PrintConsole *bot, const Config *config) {
    if (queue_count() == 0) return;

    icon_hide();
    g_dl_top = top;
    g_dl_bot = bot;
    static const ShopDownloadUI ui = { text_dl_begin, progress_cb, NULL };
    int ok, fail;
    shop_run_queue(config, &ui, &ok, &fail);

    draw_queue_summary(top, ok, fail);
    while (1) {
        app_vblank();
        scanKeys();
        if (keysDown()) break;
    }
}

/* ---- paged catalog state ---- */
static Catalog g_page;          /* the one page currently in RAM */
static int     g_loaded_page;   /* which page g_page holds (-1 = none) */

static void show_loading(PrintConsole *bot) {
    consoleSelect(bot);
    iprintf("\x1b[2J\x1b[11;10H" COL_HINT "Loading..." COL_RESET);
}

static bool ensure_page(PrintConsole *bot, const Config *config,
                        const char *server_cat, const char *query, int p) {
    if (p == g_loaded_page) return true;
    show_loading(bot);
    if (catalog_fetch_page(&g_page, config->server, config->port,
                           server_cat, query, p) < 0)
        return false;
    icons_fetch_page(config->server, config->port, server_cat, query, p);
    g_loaded_page = p;
    return true;
}

static void redraw(PrintConsole *top, PrintConsole *bot, const Category *cat,
                   const char *query, int sel_row, int total_pages) {
    const Title *t = &g_page.titles[sel_row];
    draw_list(bot, &g_page, cat, query, sel_row, g_loaded_page, total_pages);
    draw_detail(top, t, queue_contains(t, cat), queue_count());
    icon_show(sel_row);
}

#define NAV_BACK  (-1)
#define NAV_EXIT  (-2)

static int search_input(PrintConsole *top, PrintConsole *bot,
                        char *out, int out_size);

/* Browse one category's paged list, optionally filtered by `query`.
   Returns NAV_BACK (B) or NAV_EXIT (START). */
static int browse_category(PrintConsole *top, PrintConsole *bot,
                           const Config *config, const Category *cat,
                           const char *initial_query, int start_sel) {
    char query[40];
    snprintf(query, sizeof(query), "%s", initial_query ? initial_query : "");
    int selected = (start_sel >= 0) ? start_sel : 0;

    for (;;) {   /* re-entered whenever the search query changes (Y) */
        int total_count = catalog_total(config->server, config->port,
                                        cat->server_cat, query);
        consoleSelect(bot);
        if (total_count < 0) {
            iprintf("\x1b[2J\x1b[10;3H" COL_RED "Couldn't load this list." COL_RESET);
            while (1) { app_vblank(); scanKeys(); if (keysDown()) break; }
            return NAV_BACK;
        }
        if (total_count == 0) {
            iprintf("\x1b[2J\x1b[10;5H" COL_HINT "%s" COL_RESET,
                    query[0] ? "No matches found." : "No titles here.");
            while (1) { app_vblank(); scanKeys(); if (keysDown()) break; }
            return NAV_BACK;
        }

        int total_pages = (total_count + PAGE_SIZE - 1) / PAGE_SIZE;
        if (total_pages < 1) total_pages = 1;
        if (selected < 0 || selected >= total_count) selected = 0;
        g_loaded_page = -1;

        if (!ensure_page(bot, config, cat->server_cat, query, selected / PAGE_SIZE)) {
            iprintf("\x1b[2J\x1b[10;4H" COL_RED "Failed to load page." COL_RESET);
            while (1) { app_vblank(); scanKeys(); if (keysDown()) break; }
            return NAV_BACK;
        }
        redraw(top, bot, cat, query, selected - g_loaded_page * PAGE_SIZE, total_pages);

        bool requery = false;
        while (!requery) {
            app_vblank();
            scanKeys();
            shop_stir();
            uint32_t keys = keysDown();

            if (keys & KEY_START) return NAV_EXIT;
            if (keys & KEY_B)      return NAV_BACK;

            int prev = selected;
            if ((keys & KEY_UP)   && selected > 0)               selected--;
            if ((keys & KEY_DOWN) && selected < total_count - 1) selected++;
            if (keys & KEY_L) { selected -= PAGE_SIZE; if (selected < 0) selected = 0; }
            if (keys & KEY_R) { selected += PAGE_SIZE;
                                if (selected > total_count - 1) selected = total_count - 1; }

            if (selected != prev) {
                int pg = selected / PAGE_SIZE;
                if (pg != g_loaded_page &&
                    !ensure_page(bot, config, cat->server_cat, query, pg))
                    selected = prev;
                redraw(top, bot, cat, query, selected - g_loaded_page * PAGE_SIZE,
                       total_pages);
            }

            if (keys & KEY_A) {
                int row = selected - g_loaded_page * PAGE_SIZE;
                queue_toggle(&g_page.titles[row], cat);
                redraw(top, bot, cat, query, row, total_pages);
            }

            if ((keys & KEY_X) && queue_count() > 0) {
                run_queue(top, bot, config);
                redraw(top, bot, cat, query, selected - g_loaded_page * PAGE_SIZE,
                       total_pages);
            }

            /* Y searches within the current category (works for DS and VC). */
            if (keys & KEY_Y) {
                char q2[40];
                if (search_input(top, bot, q2, sizeof(q2))) {
                    snprintf(query, sizeof(query), "%s", q2);
                    selected = 0;
                    requery  = true;       /* re-enter the outer loop */
                } else {
                    redraw(top, bot, cat, query,
                           selected - g_loaded_page * PAGE_SIZE, total_pages);
                }
            }
        }
    }
}

/* Vertical menu. Returns the chosen index, NAV_BACK (B), or NAV_EXIT (START). */
static int menu_select(PrintConsole *top, PrintConsole *bot, const char *title,
                       const char *const *opts, int n, bool allow_back) {
    icon_hide();
    int sel = 0;

    consoleSelect(top);
    iprintf("\x1b[2J");
    iprintf("\x1b[6;7H" COL_TITLE "Nintendo DS Shop" COL_RESET);
    iprintf("\x1b[9;9H" COL_HINT "Welcome!" COL_RESET);

    while (1) {
        consoleSelect(bot);
        iprintf("\x1b[2J");
        iprintf("\x1b[0;0H" COL_TITLE "%-26.26s" COL_RESET, title);
        iprintf("\x1b[1;0H" COL_DIM "----------------------------" COL_RESET);
        for (int i = 0; i < n; i++) {
            iprintf("\x1b[%d;0H", 4 + i * 2);
            if (i == sel)
                iprintf(COL_SELECT "> %-26.26s" COL_RESET, opts[i]);
            else
                iprintf(COL_RESET "  %-26.26s" COL_RESET, opts[i]);
        }
        iprintf("\x1b[23;0H" COL_HINT "[A]Select%s[START]Exit" COL_RESET,
                allow_back ? " [B]Back " : "  ");

        bool dirty = false;
        while (!dirty) {
            app_vblank();
            scanKeys();
            shop_stir();
            uint32_t keys = keysDown();
            if (keys & KEY_START)                  return NAV_EXIT;
            if (allow_back && (keys & KEY_B))       return NAV_BACK;
            if ((keys & KEY_UP)   && sel > 0)     { sel--; dirty = true; }
            if ((keys & KEY_DOWN) && sel < n - 1) { sel++; dirty = true; }
            if (keys & KEY_A)                      return sel;
        }
    }
}

/* ---- on-screen keyboard (d-pad driven) ----
   Fills `out` with a search string. Returns true to search, false to cancel. */
#define KB_ROWS 4
#define KB_COLS 10
static const char *const KB_GRID[KB_ROWS] = {
    "ABCDEFGHIJ",
    "KLMNOPQRST",
    "UVWXYZ0123",
    "456789 .-'",
};

static int search_input(PrintConsole *top, PrintConsole *bot,
                        char *out, int out_size) {
    icon_hide();
    int len = 0;
    out[0] = '\0';
    int cx = 0, cy = 0;
    bool dirty = true;

    consoleSelect(top);
    iprintf("\x1b[2J");
    iprintf("\x1b[6;9H" COL_TITLE "Search" COL_RESET);
    iprintf("\x1b[9;4H" COL_HINT "Type a game name, then" COL_RESET);
    iprintf("\x1b[10;6H" COL_HINT "press X to search." COL_RESET);

    while (1) {
        if (dirty) {
            consoleSelect(bot);
            iprintf("\x1b[2J");
            iprintf("\x1b[0;0H" COL_TITLE "Search" COL_RESET);
            iprintf("\x1b[2;0H" COL_DIM ">" COL_RESET COL_SELECT "%-26.26s" COL_RESET,
                    out[0] ? out : "");
            iprintf("\x1b[3;0H" COL_DIM "----------------------------" COL_RESET);
            for (int r = 0; r < KB_ROWS; r++) {
                iprintf("\x1b[%d;2H", 6 + r * 2);
                for (int col = 0; col < KB_COLS; col++) {
                    char ch = KB_GRID[r][col];
                    if (r == cy && col == cx)
                        iprintf(COL_SELECT "[%c]" COL_RESET, ch);
                    else
                        iprintf(COL_RESET " %c " COL_RESET, ch);
                }
            }
            iprintf("\x1b[22;0H" COL_HINT "[A]Type [B]Del  [Y]Clear   " COL_RESET);
            iprintf("\x1b[23;0H" COL_HINT "[X]Search   [START]Cancel  " COL_RESET);
            dirty = false;
        }

        app_vblank();
        scanKeys();
        shop_stir();
        uint32_t keys = keysDown();

        if (keys & KEY_START) return 0;                 /* cancel */
        if (keys & KEY_X)      return (len > 0) ? 1 : 0; /* search if non-empty */

        if ((keys & KEY_UP)    && cy > 0)            { cy--; dirty = true; }
        if ((keys & KEY_DOWN)  && cy < KB_ROWS - 1)  { cy++; dirty = true; }
        if ((keys & KEY_LEFT)  && cx > 0)            { cx--; dirty = true; }
        if ((keys & KEY_RIGHT) && cx < KB_COLS - 1)  { cx++; dirty = true; }

        if (keys & KEY_A) {
            if (len < out_size - 1) {
                out[len++] = KB_GRID[cy][cx];
                out[len] = '\0';
                dirty = true;
            }
        }
        if (keys & KEY_B) {
            if (len > 0) { out[--len] = '\0'; dirty = true; }
        }
        if (keys & KEY_Y) {
            len = 0; out[0] = '\0'; dirty = true;
        }
    }
}

/* ---- top-level shop: homepage -> submenu -> category browsing ---- */

/* What a menu entry does with its list. */
#define ACT_BROWSE  0
#define ACT_RANDOM  1
#define ACT_SEARCH  2
#define ACT_PICK    3     /* VC "Search": pick the system first */

typedef struct {
    const char     *label;
    const Category *cat;
    int             action;
} MenuEntry;

static int run_entry(PrintConsole *top, PrintConsole *bot, const Config *config,
                     const MenuEntry *e) {
    if (e->action == ACT_SEARCH) {
        char query[32];
        if (!search_input(top, bot, query, sizeof(query))) return NAV_BACK;
        return browse_category(top, bot, config, e->cat, query, 0);
    }
    if (e->action == ACT_RANDOM) {
        int tot = catalog_total(config->server, config->port, e->cat->server_cat, "");
        return browse_category(top, bot, config, e->cat, "", tot > 0 ? (int)shop_random(tot) : 0);
    }
    return browse_category(top, bot, config, e->cat, "", 0);
}

static int run_menu(PrintConsole *top, PrintConsole *bot, const Config *config,
                    const char *title, const MenuEntry *entries, int n) {
    const char *labels[8];
    for (int i = 0; i < n; i++) labels[i] = entries[i].label;

    while (1) {
        int sel = menu_select(top, bot, title, labels, n, true);
        if (sel < 0) return sel;                      /* NAV_BACK / NAV_EXIT */

        const MenuEntry *e = &entries[sel];
        int r;
        if (e->action == ACT_PICK) {
            static const MenuEntry vc_search[] = {
                {"Nintendo Ent. System", &CAT_NES, ACT_SEARCH},
                {"Game Boy",             &CAT_GB,  ACT_SEARCH},
                {"Game Boy Color",       &CAT_GBC, ACT_SEARCH},
                {"Game Boy Advance",     &CAT_GBA, ACT_SEARCH},
            };
            r = run_menu(top, bot, config, "Search which system?", vc_search, 4);
        } else {
            r = run_entry(top, bot, config, e);
        }
        if (r == NAV_EXIT) return NAV_EXIT;
    }
}

void text_ui_run(PrintConsole *top, PrintConsole *bot, const Config *config) {
    icon_setup();

    static const MenuEntry ds_menu[] = {
        {"Popular Titles", &CAT_POPULAR, ACT_BROWSE},
        {"Random Title",   &CAT_DS,      ACT_RANDOM},
        {"Search",         &CAT_DS,      ACT_SEARCH},
        {"All DS Titles",  &CAT_DS,      ACT_BROWSE},
        {"DSiWare",        &CAT_DSIWARE, ACT_BROWSE},
    };
    static const MenuEntry vc_menu[] = {
        {"Nintendo Ent. System", &CAT_NES, ACT_BROWSE},
        {"Game Boy",             &CAT_GB,  ACT_BROWSE},
        {"Game Boy Color",       &CAT_GBC, ACT_BROWSE},
        {"Game Boy Advance",     &CAT_GBA, ACT_BROWSE},
        {"Search",               NULL,     ACT_PICK},
    };
    static const MenuEntry theme_menu[] = {
        {"DSi Menu",     &CAT_THEME_DSI, ACT_BROWSE},
        {"3DS Menu",     &CAT_THEME_3DS, ACT_BROWSE},
        {"R4 Menu",      &CAT_THEME_R4,  ACT_BROWSE},
        {"Wood (akmenu)",&CAT_THEME_AK,  ACT_BROWSE},
    };
    static const char *home_opts[] = {"Nintendo DS & DSiWare", "Virtual Console",
                                      "TWiLight Menu Themes"};

    while (1) {
        int home = menu_select(top, bot, "Nintendo DS Shop", home_opts, 3, false);
        if (home == NAV_EXIT) return;
        if (home < 0) continue;          /* no "back" from the homepage */

        int r;
        if (home == 0)      r = run_menu(top, bot, config, "Nintendo DS & DSiWare", ds_menu, 5);
        else if (home == 1) r = run_menu(top, bot, config, "Virtual Console", vc_menu, 5);
        else                r = run_menu(top, bot, config, "TWiLight Menu Themes", theme_menu, 4);
        if (r == NAV_EXIT) return;
    }
}
