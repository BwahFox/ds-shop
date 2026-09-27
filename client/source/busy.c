/*
 * The "still working" spinner: a ring of 8 dots in the top screen's corner,
 * like the DSi's, shown while a network request takes more than a moment.
 *
 * It's one main-engine sprite whose dots each use their own palette entry,
 * and the VBlank interrupt animates it by rotating those 8 colours. So it
 * keeps turning while the main loop is stuck in a slow call, and it stops
 * only if the DS itself stops.
 *
 * When the DSi Shop's own sprites are on the SD card (anim.c), its real wait
 * icon is shown instead of the ring.
 *
 * It writes the sprite registers directly rather than through libnds's oam*
 * functions: their oamUpdate() copies with DMA, which isn't safe from an
 * interrupt while the main loop is DMA-ing the screens (and didn't show up).
 * The same interrupt also runs the download animation (anim_vblank).
 */
#include <nds.h>
#include "anim.h"
#include "busy.h"
#include "gfx.h"

#define DOTS        8
#define SPIN_X      (SCR_W - 30)
#define SPIN_Y      (SCR_H - 30)
#define SHOW_AFTER  15          /* frames: quick requests don't flash it */
#define STEP_FRAMES 5           /* frames per dot */

static volatile bool g_on;
static int  g_frames;
static bool g_shown;

static void set_shown(bool shown) {
    if (anim_has_wait())  anim_wait_draw(g_frames, shown);
    else if (shown)       OAM[0] &= ~ATTR0_DISABLED;
    else                  OAM[0] |= ATTR0_DISABLED;
    g_shown = shown;
}

/* the head dot, a fading tail, and the rest dim */
static u16 dot_color(int behind) {
    static const u16 tail[4] = { C_ACCENT_DARK, C_ACCENT, RGB(128, 208, 244), RGB(176, 222, 246) };
    return behind < 4 ? tail[behind] : C_BORDER;
}

static void busy_vblank(void) {
    anim_vblank();
    if (!g_on) {
        if (g_shown) set_shown(false);
        g_frames = 0;
        return;
    }
    if (++g_frames < SHOW_AFTER) return;
    if (!g_shown || anim_has_wait()) set_shown(true);
    if (anim_has_wait()) return;
    int head = (g_frames / STEP_FRAMES) % DOTS;
    for (int i = 0; i < DOTS; i++)
        SPRITE_PALETTE[1 + i] = dot_color((head - i + DOTS) % DOTS);
}

void busy_init(void) {
    /* sprites on, tiles laid out one after another; every sprite off */
    REG_DISPCNT |= DISPLAY_SPR_ACTIVE | DISPLAY_SPR_1D;
    for (int i = 0; i < 128; i++) OAM[i * 4] = ATTR0_DISABLED;

    /* draw the ring: 32x32 at 8bpp is 4x4 tiles of 8x8, one after another */
    static u8 px[32 * 32];
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            u8 c = 0;
            for (int i = 0; i < DOTS; i++) {
                /* dot i sits at 45*i degrees, clockwise from the top */
                static const s8 cx[DOTS] = { 0, 7, 10, 7, 0, -7, -10, -7 };
                static const s8 cy[DOTS] = { -10, -7, 0, 7, 10, 7, 0, -7 };
                int dx = 2 * x + 1 - 32 - 2 * cx[i], dy = 2 * y + 1 - 32 - 2 * cy[i];
                if (dx * dx + dy * dy <= 30) c = (u8)(1 + i);     /* radius ~2.7 px */
            }
            px[((y / 8) * 4 + x / 8) * 64 + (y % 8) * 8 + x % 8] = c;
        }
    DC_FlushRange(px, sizeof(px));
    dmaCopy(px, SPRITE_GFX, sizeof(px));            /* tile 0 */

    OAM[0] = ATTR0_DISABLED | ATTR0_COLOR_256 | ATTR0_SQUARE | SPIN_Y;
    OAM[1] = ATTR1_SIZE_32 | SPIN_X;
    OAM[2] = ATTR2_PRIORITY(0) | 0;

    irqSet(IRQ_VBLANK, busy_vblank);
    irqEnable(IRQ_VBLANK);
}

void busy_set(bool on) {
    g_on = on;
}
