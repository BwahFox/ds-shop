#include "gui.h"
#include "app.h"
#include "assets.h"
#include "shop.h"
#include "music.h"
#include <stdlib.h>

/* ---- input ---- */

static bool g_touching = false;
static int  g_last_tx, g_last_ty, g_start_x, g_start_y;

void gui_input(Input *in) {
    app_vblank();
    scanKeys();
    shop_stir();

    in->down = keysDown();
    in->held = keysHeld();
    if (in->down & KEY_SELECT) music_toggle();

    bool touching = (in->held & KEY_TOUCH) != 0;
    if (touching) {
        touchPosition tp;
        touchRead(&tp);
        g_last_tx = tp.px;
        g_last_ty = tp.py;
    }
    in->touch_down = touching && !g_touching;
    in->touch_up   = !touching && g_touching;
    in->touch_held = touching;
    if (in->touch_down) { g_start_x = g_last_tx; g_start_y = g_last_ty; }
    g_touching = touching;

    in->tx = g_last_tx;
    in->ty = g_last_ty;
    in->sx = g_start_x;
    in->sy = g_start_y;
}

/* ---- widgets ---- */

static int widget_at(const WidgetSet *ws, int x, int y) {
    for (int i = 0; i < ws->n; i++)
        if (!ws->w[i].disabled && rect_hit(ws->w[i].r, x, y)) return i;
    return -1;
}

/* Spatial D-pad navigation: the nearest enabled widget in that direction,
   preferring ones that are straight ahead. */
static int widget_neighbour(const WidgetSet *ws, int from, int dx, int dy) {
    const Rect *a = &ws->w[from].r;
    int ax = a->x + a->w / 2, ay = a->y + a->h / 2;
    int best = -1, best_cost = 1 << 30;
    for (int i = 0; i < ws->n; i++) {
        if (i == from || ws->w[i].disabled) continue;
        const Rect *b = &ws->w[i].r;
        int bx = b->x + b->w / 2, by = b->y + b->h / 2;
        int along = (bx - ax) * dx + (by - ay) * dy;       /* distance ahead */
        int side  = abs((bx - ax) * dy) + abs((by - ay) * dx);
        if (along <= 0) continue;
        int cost = along + side * 3;
        if (cost < best_cost) { best_cost = cost; best = i; }
    }
    return best;
}

int widgets_update(WidgetSet *ws, const Input *in, bool *dirty) {
    /* touch: activate on release over the widget that was pressed */
    if (in->touch_down) {
        int i = widget_at(ws, in->tx, in->ty);
        if (i != ws->pressed) { ws->pressed = i; *dirty = true; }
        if (i >= 0 && ws->dpad) ws->focus = i;
    } else if (in->touch_up && ws->pressed >= 0) {
        int i = ws->pressed;
        ws->pressed = -1;
        *dirty = true;
        if (rect_hit(ws->w[i].r, in->tx, in->ty)) return ws->w[i].id;
    }

    if (!ws->dpad || ws->n == 0) return -1;

    if (ws->focus < 0 || ws->focus >= ws->n || ws->w[ws->focus].disabled) {
        if (in->down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_A)) {
            for (int i = 0; i < ws->n; i++)
                if (!ws->w[i].disabled) { ws->focus = i; *dirty = true; break; }
        }
        return -1;
    }

    int dx = 0, dy = 0;
    if (in->down & KEY_UP)    dy = -1;
    if (in->down & KEY_DOWN)  dy =  1;
    if (in->down & KEY_LEFT)  dx = -1;
    if (in->down & KEY_RIGHT) dx =  1;
    if (dx || dy) {
        int n = widget_neighbour(ws, ws->focus, dx, dy);
        if (n >= 0) { ws->focus = n; *dirty = true; }
    }
    if (in->down & KEY_A) return ws->w[ws->focus].id;
    return -1;
}

void widget_draw(const Widget *w, bool focused, bool pressed) {
    Rect r = w->r;
    u16 fill = C_WHITE, border = C_BORDER, text = C_TEXT;
    if (w->style == STYLE_PRIMARY) { fill = C_ACCENT; border = C_ACCENT_DARK; text = C_TEXT_ON; }
    if (pressed)  { fill = (w->style == STYLE_PRIMARY) ? C_ACCENT_DARK : C_ACCENT_PALE; }
    if (w->disabled) { fill = C_BG; border = C_BORDER; text = C_TEXT_DIM; }

    int radius = (w->style == STYLE_KEY) ? 4 : 7;
    if (focused && !w->disabled) {
        /* a 2px blue ring marks the D-pad focus */
        gfx_round_rect(SCR_BOT, r.x - 2, r.y - 2, r.w + 4, r.h + 4, radius + 2,
                       C_ACCENT, C_ACCENT);
    }
    gfx_round_rect(SCR_BOT, r.x, r.y, r.w, r.h, radius, fill, border);

    if (w->style == STYLE_TILE) {
        /* bag on the left, label + sublabel on the right */
        gfx_image(SCR_BOT, r.x + 8, r.y + (r.h - bag_icon_h) / 2,
                  bag_icon_w, bag_icon_h, bag_icon);
        int tx = r.x + 16 + bag_icon_w;
        if (w->sublabel) {
            gfx_text_fit(SCR_BOT, &font_body, tx, r.y + 4, r.w - (tx - r.x) - 6, w->label, text);
            gfx_text_fit(SCR_BOT, &font_small, tx, r.y + 4 + font_body.height,
                         r.w - (tx - r.x) - 6, w->sublabel, C_TEXT_DIM);
        } else {
            gfx_text_fit(SCR_BOT, &font_body, tx, r.y + (r.h - font_body.height) / 2,
                         r.w - (tx - r.x) - 6, w->label, text);
        }
        return;
    }

    const Font *f = (w->style == STYLE_KEY) ? &font_body : &font_small;
    int tw = gfx_text_width(f, w->label);
    if (tw > r.w - 6) tw = r.w - 6;
    gfx_text_fit(SCR_BOT, f, r.x + (r.w - tw) / 2, r.y + (r.h - f->height) / 2,
                 r.w - 6, w->label, text);
}

void widgets_draw(const WidgetSet *ws) {
    for (int i = 0; i < ws->n; i++)
        widget_draw(&ws->w[i], ws->dpad && i == ws->focus, i == ws->pressed);
}

/* ---- chrome ---- */

void gui_bottom_frame(const char *title, const char *note) {
    gfx_bottom_background();
    gfx_fill(SCR_BOT, 0, 0, SCR_W, HEADER_H, C_ACCENT);
    gfx_hline(SCR_BOT, 0, HEADER_H, SCR_W, C_ACCENT_DARK);
    int notew = note ? gfx_text_width(&font_small, note) + 8 : 0;
    gfx_text_fit(SCR_BOT, &font_body, 6, (HEADER_H - font_body.height) / 2,
                 SCR_W - 12 - notew, title, C_TEXT_ON);
    if (note)
        gfx_text(SCR_BOT, &font_small, SCR_W - notew, (HEADER_H - font_small.height) / 2 + 1,
                 note, C_TEXT_ON);
    /* action bar */
    gfx_fill_blend(SCR_BOT, 0, BAR_Y, SCR_W, SCR_H - BAR_Y, C_ACCENT_PALE, 10);
    gfx_hline(SCR_BOT, 0, BAR_Y, SCR_W, C_BORDER);
}

void gui_top_frame(const u16 *background, int card_y, int card_h) {
    gfx_background(SCR_TOP, background);
    if (card_h > 0) {
        gfx_fill_blend(SCR_TOP, 12, card_y, SCR_W - 24, card_h, C_WHITE, 12);
        gfx_hline(SCR_TOP, 12, card_y, SCR_W - 24, C_BORDER);
        gfx_hline(SCR_TOP, 12, card_y + card_h - 1, SCR_W - 24, C_BORDER);
    }
}

void gui_status(const char *title, const char *line1, const char *line2) {
    gui_top_frame(bg_home_top, 64, 64);
    gfx_text_center(SCR_TOP, &font_title, SCR_W / 2, 74, "Nintendo DS Shop", C_TEXT);
    gfx_text_center(SCR_TOP, &font_small, SCR_W / 2, 100, "for every Nintendo DS", C_TEXT_DIM);

    gfx_bottom_background();
    gfx_text_center(SCR_BOT, &font_title, SCR_W / 2, 60, title, C_TEXT);
    if (line1) gfx_text_center(SCR_BOT, &font_body, SCR_W / 2, 90, line1, C_TEXT_DIM);
    if (line2) gfx_text_center(SCR_BOT, &font_body, SCR_W / 2, 90 + font_body.height, line2, C_TEXT_DIM);
    gfx_present(3);
}

void gui_message(const char *title, const char *line1, const char *line2) {
    gui_status(title, line1, line2);
    Widget ok = { {88, 140, 80, 28}, "OK", NULL, 1, STYLE_PRIMARY, false };
    WidgetSet ws = { &ok, 1, 0, -1, true };
    widgets_draw(&ws);
    gfx_present(2);
    Input in;
    for (;;) {
        gui_input(&in);
        bool dirty = false;
        if (widgets_update(&ws, &in, &dirty) >= 0 || (in.down & (KEY_B | KEY_START))) return;
        if (dirty) { widgets_draw(&ws); gfx_present(2); }
    }
}
