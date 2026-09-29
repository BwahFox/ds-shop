/*
 * The DSi Shop's own sprites, from ANIM_PATH (see tools/make_anim.py): the
 * download animation on the bottom screen and the "please wait" icon on the
 * top one.
 *
 * Their tiles and sprite attributes are already in the DS sprite hardware's
 * format, so loading them is a copy into sprite VRAM, and showing a frame
 * ("cell") is writing its few sprites into OAM at a position. That happens in
 * the VBlank interrupt, like the spinner in busy.c, so the animation keeps
 * going while the main loop is busy downloading and costs it nothing.
 *
 * The file holds only the pictures. Which cells follow which, and where they
 * go, is below: the box sequences come from the DSi Shop's animation file,
 * the characters' positions from its layout, and the walking and timing
 * around them is ours (the Shop did that part in code).
 */
#include <nds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "anim.h"
#include "gfx.h"

/* ---- sprite sets ---- */

typedef struct {
    bool ok;
    int ncells;
    const u16 *cells;        /* per cell: first sprite, sprite count */
    const u16 *spr;          /* per sprite: attr0, attr1, attr2 */
    int nspr;
    int tile_base;           /* where its tiles went, in 128-byte units */
    volatile u16 *oam;       /* which engine it's drawn on */
    int slot0, slots;        /* the OAM entries it may use */
} Set;

static Set g_dl, g_wait;
static u8 *g_file;

#define WAIT_TILE_BASE  8    /* after busy.c's ring (1 KB at tile 0) */

static void copy16(volatile u16 *dst, const void *src, int bytes) {
    const u16 *s = src;                     /* VRAM takes 16-bit writes only */
    for (int i = 0; i < bytes / 2; i++) dst[i] = s[i];
}

/* parse one set at *p; false if the file is cut short or doesn't add up */
static bool read_set(const u8 **p, const u8 *end, char name[9], Set *set,
                     const u8 **pal, const u8 **tiles, u32 *tile_bytes) {
    const u8 *q = *p;
    if (end - q < 20 + 512) return false;
    memcpy(name, q, 8);
    name[8] = '\0';
    u32 hdr[3];
    memcpy(hdr, q + 8, sizeof(hdr));
    *tile_bytes = hdr[0];
    set->ncells = (int)hdr[1];
    set->nspr = (int)hdr[2];
    q += 20;
    *pal = q;
    q += 512;
    u32 spr_bytes = ((u32)set->nspr * 6 + 3) & ~3u;
    if (hdr[0] > 64 * 1024 || hdr[1] > 256 || hdr[2] > 1024
        || (u32)(end - q) < hdr[0] + hdr[1] * 4 + spr_bytes)
        return false;
    *tiles = q;
    q += hdr[0];
    set->cells = (const u16 *)q;
    q += hdr[1] * 4;
    set->spr = (const u16 *)q;
    q += spr_bytes;
    for (int i = 0; i < set->ncells; i++)
        if (set->cells[i * 2] + set->cells[i * 2 + 1] > set->nspr) return false;
    *p = q;
    return true;
}

void anim_load(void) {
    FILE *f = fopen(ANIM_PATH, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 12 || size > 256 * 1024 || !(g_file = malloc((size_t)size))
        || fread(g_file, (size_t)size, 1, f) != 1) {
        fclose(f);
        free(g_file);
        g_file = NULL;
        return;
    }
    fclose(f);

    const u8 *p = g_file, *end = g_file + size;
    u32 hdr[2];
    memcpy(hdr, p + 4, sizeof(hdr));
    if (memcmp(p, "DSAN", 4) != 0 || hdr[0] != 1) return;
    p += 12;
    for (u32 i = 0; i < hdr[1]; i++) {
        char name[9];
        Set s = { 0 };
        const u8 *pal, *tiles;
        u32 tile_bytes;
        if (!read_set(&p, end, name, &s, &pal, &tiles, &tile_bytes)) return;

        if (strcmp(name, "dl") == 0) {
            /* bottom screen: sub-engine sprites (VRAM_D, see gfx_init) */
            copy16(SPRITE_PALETTE_SUB, pal, 512);
            copy16(SPRITE_GFX_SUB, tiles, (int)tile_bytes);
            s.tile_base = 0;
            s.oam = OAM_SUB;
            s.slot0 = 0;
            s.slots = 32;
            for (int k = 0; k < 128; k++) OAM_SUB[k * 4] = ATTR0_DISABLED;
            REG_DISPCNT_SUB |= DISPLAY_SPR_ACTIVE | DISPLAY_SPR_1D | DISPLAY_SPR_1D_SIZE_128;
            s.ok = true;
            g_dl = s;
        } else if (strcmp(name, "wait") == 0 && tile_bytes <= 16 * 1024) {
            /* top screen, next to the ring, which it replaces (busy.c) */
            copy16(SPRITE_PALETTE, pal, 512);
            copy16(SPRITE_GFX + WAIT_TILE_BASE * 64, tiles, (int)tile_bytes);
            s.tile_base = WAIT_TILE_BASE;
            s.oam = OAM;
            s.slot0 = 1;
            s.slots = 8;
            REG_DISPCNT = (REG_DISPCNT & ~(3 << 20)) | DISPLAY_SPR_1D_SIZE_128;
            s.ok = true;
            g_wait = s;
        }
    }
}

/* ---- drawing a cell ---- */

static const u8 spr_w[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
static const u8 spr_h[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };

/* Put `cell`'s sprites, centred on (x, y), into set's OAM from *slot on.
   hflip mirrors the whole cell. Sprites fully off the screen are skipped. */
static void put(const Set *s, int *slot, int cell, int x, int y, bool hflip) {
    if (cell < 0 || cell >= s->ncells) return;
    int first = s->cells[cell * 2], n = s->cells[cell * 2 + 1];
    for (int i = 0; i < n && *slot < s->slot0 + s->slots; i++) {
        const u16 *a = &s->spr[(first + i) * 3];
        int shape = a[0] >> 14, size = a[1] >> 14;
        if (shape > 2) continue;
        int w = spr_w[shape][size], h = spr_h[shape][size];
        int sx = a[1] & 0x1FF, sy = (s8)(a[0] & 0xFF);
        if (sx >= 256) sx -= 512;
        u16 a1 = a[1];
        if (hflip) {
            sx = -sx - w;
            a1 ^= ATTR1_FLIP_X;
        }
        int X = x + sx, Y = y + sy;
        if (X >= SCR_W || X + w <= 0 || Y >= SCR_H || Y + h <= 0) continue;
        volatile u16 *o = &s->oam[*slot * 4];
        o[0] = (a[0] & 0xFF00) | (Y & 0xFF);
        o[1] = (a1 & 0xFE00) | (X & 0x1FF);
        o[2] = (a[2] & 0xFC00) | ((a[2] & 0x3FF) + s->tile_base);
        (*slot)++;
    }
}

/* switch off the set's OAM entries from `slot` on */
static void clear_from(const Set *s, int slot) {
    for (; slot < s->slot0 + s->slots; slot++) s->oam[slot * 4] = ATTR0_DISABLED;
}

/* ---- the wait icon ---- */

bool anim_has_wait(void) { return g_wait.ok; }

void anim_wait_draw(int frame, bool shown) {
    if (!g_wait.ok) return;
    int slot = g_wait.slot0;
    if (shown)          /* 8 cells, 4 frames each (the Shop's own sequence) */
        put(&g_wait, &slot, (frame / 4) % 8, 30 + 16 - 34, SCR_H - 30 + 16, false);
    clear_from(&g_wait, slot);
}

/* ---- the download animation ---- */

#define BOX_X  128
#define BOX_DY (-24)          /* the Shop's layout is centred at y 96; ours at 72 */

typedef struct { u8 cell; u8 frames; } Step;

/* the box's sequences from the Shop's animation file */
static const Step seq_appear[] = { { 66, 20 }, { 67, 4 }, { 68, 4 }, { 69, 4 } };
static const Step seq_close[] = {
    { 10, 2 }, { 70, 4 }, { 71, 4 }, { 72, 4 }, { 73, 20 },
    { 73, 4 }, { 20, 2 }, { 73, 4 }, { 20, 2 }, { 73, 4 }, { 20, 2 },
    { 73, 4 }, { 20, 2 }, { 73, 4 }, { 20, 2 }, { 73, 4 },
};
static const Step seq_done[] = { { 73, 20 }, { 65, 4 }, { 20, 20 }, { 65, 4 } };

/* the cell at frame t; *over is set once t is past the end */
static int seq_cell(const Step *seq, int n, int t, bool loop, bool *over) {
    int total = 0;
    for (int i = 0; i < n; i++) total += seq[i].frames;
    *over = t >= total;
    if (*over && !loop) return seq[n - 1].cell;
    t %= total;
    for (int i = 0; i < n; i++) {
        if (t < seq[i].frames) return seq[i].cell;
        t -= seq[i].frames;
    }
    return seq[n - 1].cell;
}

/* Each character's cells at their layout positions: walking in with the
   data, throwing it into the box, then walking back without it (the same
   cells mirrored, so they face the way they're going). */
typedef struct { s16 x, y; u8 cell; } Pos;
typedef struct {
    bool from_right;
    Pos walk[3], thr[5], back[3];
} Actor;

static const Actor actors[4] = {
    { false, /* Mario */
      { { 9, 91, 21 }, { 9, 90, 22 }, { 9, 90, 23 } },
      { { 96, 92, 24 }, { 101, 88, 25 }, { 106, 85, 26 }, { 111, 89, 27 }, { 96, 95, 28 } },
      { { 96, 94, 29 }, { 96, 94, 30 }, { 96, 94, 31 } } },
    { true, /* Luigi */
      { { 247, 90, 32 }, { 247, 90, 33 }, { 247, 90, 34 } },
      { { 160, 91, 35 }, { 154, 88, 36 }, { 149, 85, 37 }, { 144, 89, 38 }, { 159, 95, 39 } },
      { { 160, 94, 40 }, { 160, 93, 41 }, { 160, 93, 42 } } },
    { false, /* Peach */
      { { 8, 89, 43 }, { 8, 89, 44 }, { 8, 89, 45 } },
      { { 96, 89, 46 }, { 101, 87, 47 }, { 108, 85, 48 }, { 111, 89, 49 }, { 96, 95, 50 } },
      { { 96, 95, 51 }, { 96, 95, 52 }, { 95, 95, 53 } } },
    { true, /* Toad */
      { { 247, 93, 54 }, { 247, 92, 55 }, { 247, 92, 56 } },
      { { 160, 94, 57 }, { 155, 88, 58 }, { 150, 86, 59 }, { 145, 89, 60 }, { 160, 95, 61 } },
      { { 160, 94, 62 }, { 160, 93, 63 }, { 160, 93, 64 } } },
};

static const u8 thr_frames[5] = { 8, 6, 6, 6, 16 };
static const u8 walk_cycle[4] = { 0, 1, 0, 2 };
#define WALK_STEP   8         /* frames per walking pose */
#define PAUSE       30        /* frames between characters */
#define OFF_LEFT    (-24)
#define OFF_RIGHT   (SCR_W + 24)

enum { BOX_OFF, BOX_APPEAR, BOX_FILL, BOX_CLOSE, BOX_DONE };
enum { A_PAUSE, A_IN, A_THROW, A_BACK };

static volatile int g_box = BOX_OFF, g_pct;
static int g_box_t;
static int g_actor, g_aphase, g_at, g_ax;

bool anim_has_dl(void) { return g_dl.ok; }

void anim_dl_start(void) {
    if (!g_dl.ok) return;
    int old = enterCriticalSection();
    g_box = BOX_APPEAR;
    g_box_t = 0;
    g_pct = 0;
    g_aphase = A_PAUSE;           /* the next character, after a moment */
    g_at = 0;
    leaveCriticalSection(old);
}

void anim_dl_progress(int pct) { g_pct = pct; }

void anim_dl_finish(void) {
    if (!g_dl.ok || g_box == BOX_OFF) return;
    int old = enterCriticalSection();
    g_box = BOX_CLOSE;
    g_box_t = 0;
    leaveCriticalSection(old);
}

bool anim_dl_finished(void) { return g_box == BOX_DONE || g_box == BOX_OFF; }

void anim_dl_stop(void) {
    if (!g_dl.ok) return;
    int old = enterCriticalSection();
    g_box = BOX_OFF;
    clear_from(&g_dl, g_dl.slot0);
    leaveCriticalSection(old);
}

/* move the character along; returns false while nobody is out */
static bool actor_step(int *cell, int *x, int *y, bool *flip) {
    const Actor *a = &actors[g_actor];
    int dir = a->from_right ? -1 : 1;
    g_at++;
    switch (g_aphase) {
    case A_PAUSE:
        /* a new character comes out only while the box is filling */
        if (g_box != BOX_FILL || g_at < PAUSE) return false;
        g_aphase = A_IN;
        g_at = 0;
        g_ax = a->from_right ? OFF_RIGHT : OFF_LEFT;
        /* fall through */
    case A_IN: {
        if (g_box != BOX_FILL) {                 /* the download ended */
            g_aphase = A_PAUSE;
            g_at = 0;
            return false;
        }
        const Pos *p = &a->walk[walk_cycle[(g_at / WALK_STEP) & 3]];
        g_ax += dir;
        *cell = p->cell;
        *x = g_ax;
        *y = p->y;
        *flip = false;
        if (g_ax == a->thr[0].x) {
            g_aphase = A_THROW;
            g_at = 0;
        }
        return true;
    }
    case A_THROW: {
        int t = g_at, i = 0;
        while (i < 4 && t >= thr_frames[i]) t -= thr_frames[i++];
        if (t >= thr_frames[4]) {                /* thrown: turn around */
            g_aphase = A_BACK;
            g_at = 0;
            g_ax = a->back[0].x;
            i = 4;
        }
        *cell = a->thr[i].cell;
        *x = a->thr[i].x;
        *y = a->thr[i].y;
        *flip = false;
        return true;
    }
    default: {                                   /* A_BACK */
        const Pos *p = &a->back[walk_cycle[(g_at / WALK_STEP) & 3]];
        g_ax -= dir;
        if (g_ax < OFF_LEFT || g_ax > OFF_RIGHT) {
            g_actor = (g_actor + 1) % 4;
            g_aphase = A_PAUSE;
            g_at = 0;
            return false;
        }
        *cell = p->cell;
        *x = g_ax + p->x - a->back[0].x;
        *y = p->y;
        *flip = true;
        return true;
    }
    }
}

void anim_vblank(void) {
    if (!g_dl.ok || g_box == BOX_OFF) return;

    int box = 0;
    bool over;
    g_box_t++;
    switch (g_box) {
    case BOX_APPEAR:
        box = seq_cell(seq_appear, 4, g_box_t, false, &over);
        if (over) g_box = BOX_FILL;
        break;
    case BOX_FILL: {
        int pct = g_pct < 0 ? 0 : g_pct > 100 ? 100 : g_pct;
        box = pct / 10;                          /* cells 0..10: empty..full */
        break;
    }
    case BOX_CLOSE:
        box = seq_cell(seq_close, sizeof(seq_close) / sizeof(*seq_close), g_box_t, false, &over);
        if (over) {
            g_box = BOX_DONE;
            g_box_t = 0;
        }
        break;
    default:
        box = seq_cell(seq_done, 4, g_box_t, true, &over);
        break;
    }

    /* the box goes first, so it's drawn over the data that drops into it */
    int slot = g_dl.slot0, cell, x, y;
    bool flip;
    put(&g_dl, &slot, box, BOX_X, 96 + BOX_DY, false);
    if (actor_step(&cell, &x, &y, &flip))
        put(&g_dl, &slot, cell, x, y + BOX_DY, flip);
    clear_from(&g_dl, slot);
}
