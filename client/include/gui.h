#pragma once
#include <nds.h>
#include <stdbool.h>
#include "gfx.h"
#include "config.h"

/* Widgets and screen chrome for the graphical UI. Screens are plain loops:
   poll input, update, redraw into the backbuffers when something changed,
   present. Everything works by touch and by buttons. */

/* ---- input ---- */
typedef struct {
    u32  down, held;                 /* keysDown / keysHeld                     */
    bool touch_down;                 /* stylus touched the screen this frame     */
    bool touch_held;
    bool touch_up;                   /* stylus lifted this frame                 */
    int  tx, ty;                     /* current (or, on touch_up, last) position */
    int  sx, sy;                     /* where the current touch started          */
} Input;

/* wait for the next frame and read keys + touch */
void gui_input(Input *in);

/* ---- widgets ---- */
typedef struct { s16 x, y, w, h; } Rect;

static inline bool rect_hit(Rect r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

#define STYLE_NORMAL   0     /* white button                       */
#define STYLE_PRIMARY  1     /* blue button, white text            */
#define STYLE_TILE     2     /* big home-screen tile with the bag  */
#define STYLE_KEY      3     /* keyboard key                       */

typedef struct {
    Rect        r;
    const char *label;
    const char *sublabel;    /* second line (tiles only), may be NULL */
    int         id;          /* returned when activated               */
    u8          style;
    bool        disabled;
} Widget;

/* A set of widgets on the bottom screen, with a D-pad focus and touch state. */
typedef struct {
    Widget *w;
    int     n;
    int     focus;           /* index of the focused widget, -1 = none  */
    int     pressed;         /* index held down by the stylus, -1 = none */
    bool    dpad;            /* D-pad moves the focus (off in lists)     */
} WidgetSet;

/* Handles touch (press, then release on the same widget) and, if ws->dpad,
   D-pad focus + A. Returns the activated widget's id, or -1. Sets *dirty when
   the widgets need redrawing. */
int  widgets_update(WidgetSet *ws, const Input *in, bool *dirty);
void widgets_draw(const WidgetSet *ws);
void widget_draw(const Widget *w, bool focused, bool pressed);

/* ---- chrome ---- */
#define HEADER_H   22
#define BAR_Y      160       /* bottom action bar: 160..191 */

/* bottom screen: plain background + blue header with a title and an optional
   right-aligned note (page number, queue size...) */
void gui_bottom_frame(const char *title, const char *note);
/* top screen: background image + a translucent white card */
void gui_top_frame(const u16 *background, int card_y, int card_h);

/* Full-screen status message on both screens (boot steps, errors). */
void gui_status(const char *title, const char *line1, const char *line2);
/* ...and wait for A / a tap */
void gui_message(const char *title, const char *line1, const char *line2);
