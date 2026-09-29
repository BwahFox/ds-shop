#include "gfx.h"
#include "ui.h"
#include <string.h>

/* Backbuffers live in main RAM (BSS): 2 x 96 KB. */
static u16 g_buf[2][SCR_W * SCR_H] __attribute__((aligned(32)));
static u16 g_bottom_bg[SCR_W * SCR_H] __attribute__((aligned(32)));
static int g_bg_id[2];

void gfx_init(void) {
    /* Main (top): BG3 = 16-bit bitmap in VRAM_A; VRAM_B = main sprites.
       Sub (bottom): BG3 = 16-bit bitmap in VRAM_C; VRAM_D = sub sprites.
       (The text UI uses a different layout, see main.c.) */
    videoSetMode(MODE_5_2D | DISPLAY_BG3_ACTIVE);
    videoSetModeSub(MODE_5_2D | DISPLAY_BG3_ACTIVE);
    vramSetBankA(VRAM_A_MAIN_BG_0x06000000);
    vramSetBankB(VRAM_B_MAIN_SPRITE);
    vramSetBankC(VRAM_C_SUB_BG_0x06200000);
    vramSetBankD(VRAM_D_SUB_SPRITE);

    g_bg_id[SCR_TOP] = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    g_bg_id[SCR_BOT] = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);

    /* the plain bottom-screen background: a soft white-to-grey gradient */
    for (int y = 0; y < SCR_H; y++) {
        int t = y * 16 / SCR_H;
        u16 c = RGB(252 - t, 253 - t, 255 - t / 2);
        for (int x = 0; x < SCR_W; x++) g_bottom_bg[y * SCR_W + x] = c;
    }

    gfx_clear(SCR_TOP, C_BG);
    gfx_clear(SCR_BOT, C_BG);
    gfx_present(3);
}

u16 *gfx_buffer(int scr) { return g_buf[scr]; }

void gfx_present(int mask) {
    for (int s = 0; s < 2; s++) {
        if (!(mask & (1 << s))) continue;
        DC_FlushRange(g_buf[s], sizeof(g_buf[s]));
        dmaCopy(g_buf[s], bgGetGfxPtr(g_bg_id[s]), sizeof(g_buf[s]));
        if (s == SCR_TOP) draw_wifi_signal_indicator();
    }
}

void gfx_clear(int scr, u16 color) {
    u32 c2 = color | ((u32)color << 16);
    u32 *p = (u32 *)g_buf[scr];
    for (int i = 0; i < SCR_W * SCR_H / 2; i++) p[i] = c2;
}

void gfx_background(int scr, const u16 *image) {
    memcpy(g_buf[scr], image, sizeof(g_buf[scr]));
}

void gfx_bottom_background(void) {
    memcpy(g_buf[SCR_BOT], g_bottom_bg, sizeof(g_bottom_bg));
}

/* clip a rect to the screen; false if nothing is left */
static bool clip(int *x, int *y, int *w, int *h) {
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > SCR_W) *w = SCR_W - *x;
    if (*y + *h > SCR_H) *h = SCR_H - *y;
    return *w > 0 && *h > 0;
}

void gfx_fill(int scr, int x, int y, int w, int h, u16 color) {
    if (!clip(&x, &y, &w, &h)) return;
    for (int j = 0; j < h; j++) {
        u16 *p = &g_buf[scr][(y + j) * SCR_W + x];
        for (int i = 0; i < w; i++) p[i] = color;
    }
}

/* blend c over d, a = 0..16 */
static inline u16 blend(u16 d, u16 c, int a) {
    int dr = d & 31, dg = (d >> 5) & 31, db = (d >> 10) & 31;
    int cr = c & 31, cg = (c >> 5) & 31, cb = (c >> 10) & 31;
    dr += ((cr - dr) * a) >> 4;
    dg += ((cg - dg) * a) >> 4;
    db += ((cb - db) * a) >> 4;
    return 0x8000 | dr | (dg << 5) | (db << 10);
}

void gfx_fill_blend(int scr, int x, int y, int w, int h, u16 color, int alpha) {
    if (!clip(&x, &y, &w, &h)) return;
    for (int j = 0; j < h; j++) {
        u16 *p = &g_buf[scr][(y + j) * SCR_W + x];
        for (int i = 0; i < w; i++) p[i] = blend(p[i], color, alpha);
    }
}

void gfx_hline(int scr, int x, int y, int w, u16 color) { gfx_fill(scr, x, y, w, 1, color); }
void gfx_vline(int scr, int x, int y, int h, u16 color) { gfx_fill(scr, x, y, 1, h, color); }

/* how far row `dy` (0 = outermost) of a radius-r corner is inset: the first
   pixel whose center lies inside the circle, all in doubled coordinates */
static int corner_inset(int r, int dy) {
    int vy = 2 * (r - dy) - 1;
    for (int i = 0; i < r; i++) {
        int vx = 2 * (r - i) - 1;
        if (vx * vx + vy * vy <= 4 * r * r) return i;
    }
    return r;
}

static void round_fill(int scr, int x, int y, int w, int h, int r, u16 color) {
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    for (int j = 0; j < h; j++) {
        int inset = 0;
        if (j < r)           inset = corner_inset(r, j);
        else if (j >= h - r) inset = corner_inset(r, h - 1 - j);
        gfx_fill(scr, x + inset, y + j, w - inset * 2, 1, color);
    }
}

void gfx_round_rect(int scr, int x, int y, int w, int h, int r, u16 fill, u16 border) {
    if (border != fill) {
        round_fill(scr, x, y, w, h, r, border);
        round_fill(scr, x + 1, y + 1, w - 2, h - 2, r > 0 ? r - 1 : 0, fill);
    } else {
        round_fill(scr, x, y, w, h, r, fill);
    }
}

void gfx_image(int scr, int x, int y, int w, int h, const u16 *pixels) {
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= SCR_H) continue;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            u16 c = pixels[j * w + i];
            if ((c & 0x8000) && xx >= 0 && xx < SCR_W)
                g_buf[scr][yy * SCR_W + xx] = c;
        }
    }
}

void gfx_icon(int scr, int x, int y, const void *tiles, const void *palette, int scale) {
    const u8  *t   = (const u8 *)tiles;
    const u16 *pal = (const u16 *)palette;
    for (int py = 0; py < 32; py++) {
        for (int px = 0; px < 32; px++) {
            /* 4x4 tiles of 8x8, 4bpp, 4 bytes per tile row, low nibble first */
            int tile = (py >> 3) * 4 + (px >> 3);
            u8 b = t[tile * 32 + (py & 7) * 4 + ((px & 7) >> 1)];
            int idx = (px & 1) ? (b >> 4) : (b & 15);
            if (idx == 0) continue;                         /* transparent */
            u16 c = pal[idx] | 0x8000;
            for (int sy = 0; sy < scale; sy++)
                for (int sx = 0; sx < scale; sx++) {
                    int xx = x + px * scale + sx, yy = y + py * scale + sy;
                    if (xx >= 0 && xx < SCR_W && yy >= 0 && yy < SCR_H)
                        g_buf[scr][yy * SCR_W + xx] = c;
                }
        }
    }
}

static int glyph_width(const Font *f, char ch) {
    unsigned c = (unsigned char)ch;
    if (c < f->first || c >= (unsigned)(f->first + f->count)) c = '?';
    return f->widths[c - f->first];
}

int gfx_text_width(const Font *f, const char *s) {
    int w = 0;
    while (*s) w += glyph_width(f, *s++);
    return w;
}

static int draw_glyph(int scr, const Font *f, int x, int y, char ch, u16 color) {
    unsigned c = (unsigned char)ch;
    if (c < f->first || c >= (unsigned)(f->first + f->count)) c = '?';
    c -= f->first;
    int w = f->widths[c];
    const u8 *bits = f->bits + f->offsets[c];
    for (int j = 0; j < f->height; j++) {
        int yy = y + j;
        for (int i = 0; i < w; i++) {
            int n = j * w + i;
            int a = (n & 1) ? (bits[n >> 1] >> 4) : (bits[n >> 1] & 15);
            if (!a) continue;
            int xx = x + i;
            if (xx < 0 || xx >= SCR_W || yy < 0 || yy >= SCR_H) continue;
            u16 *p = &g_buf[scr][yy * SCR_W + xx];
            *p = (a == 15) ? color : blend(*p, color, a + 1);
        }
    }
    return w;
}

int gfx_text(int scr, const Font *f, int x, int y, const char *s, u16 color) {
    while (*s) x += draw_glyph(scr, f, x, y, *s++, color);
    return x;
}

int gfx_text_fit(int scr, const Font *f, int x, int y, int maxw, const char *s, u16 color) {
    if (gfx_text_width(f, s) <= maxw) return gfx_text(scr, f, x, y, s, color);
    int dots = gfx_text_width(f, "...");
    int w = 0;
    while (*s && w + glyph_width(f, *s) + dots <= maxw) {
        w += draw_glyph(scr, f, x + w, y, *s, color);
        s++;
    }
    return gfx_text(scr, f, x + w, y, "...", color);
}

void gfx_text_center(int scr, const Font *f, int cx, int y, const char *s, u16 color) {
    gfx_text(scr, f, cx - gfx_text_width(f, s) / 2, y, s, color);
}

int gfx_text_wrap(int scr, const Font *f, int x, int y, int w, int max_lines,
                  const char *s, u16 color) {
    int lines = 0;
    while (*s && lines < max_lines) {
        /* take as many whole words as fit */
        const char *end = s, *brk = NULL;
        int lw = 0;
        while (*end && *end != '\n') {
            int cw = glyph_width(f, *end);
            if (lw + cw > w) break;
            lw += cw;
            if (*end == ' ') brk = end;
            end++;
        }
        if (*end && *end != '\n' && brk) end = brk;      /* break at the last space */
        if (end == s) end = s + 1;                        /* one huge char: force progress */

        char line[128];
        int n = end - s;
        if (n > (int)sizeof(line) - 1) n = sizeof(line) - 1;
        memcpy(line, s, n);
        line[n] = '\0';
        if (lines == max_lines - 1 && *end && *end != '\n')
            gfx_text_fit(scr, f, x, y, w, s, color);     /* last line: ellipsize the rest */
        else
            gfx_text(scr, f, x, y, line, color);

        s = end;
        while (*s == ' ' || *s == '\n') s++;
        y += f->height;
        lines++;
    }
    return lines;
}
