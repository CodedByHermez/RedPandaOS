#include "cursor.h"

#include <stdint.h>

#include "fb.h"

/* The classic arrow, 12x19: X = black outline, W = white fill, . = empty.
 * The hotspot (the pixel that clicks) is the tip at the top left. */
static const char *arrow[] = {
    "X...........",
    "XX..........",
    "XWX.........",
    "XWWX........",
    "XWWWX.......",
    "XWWWWX......",
    "XWWWWWX.....",
    "XWWWWWWX....",
    "XWWWWWWWX...",
    "XWWWWWWWWX..",
    "XWWWWWXXXXX.",
    "XWWXWWX.....",
    "XWX.XWWX....",
    "XX..XWWX....",
    "X....XWWX...",
    ".....XWWX...",
    "......XWWX..",
    "......XWWX..",
    ".......XX...",
};

#define ART_W 12
#define ART_H 19
#define PAD   2                 /* room for the drop shadow */
#define SPR_W (ART_W + PAD)
#define SPR_H (ART_H + PAD)

static uint32_t sprite[SPR_W * SPR_H];

static uint32_t art_at(int x, int y)
{
    if (x < 0 || y < 0 || x >= ART_W || y >= ART_H)
        return 0;
    char c = arrow[y][x];
    if (c == 'X')
        return 0xFF000000;      /* opaque black */
    if (c == 'W')
        return 0xFFFFFFFF;      /* opaque white */
    return 0;
}

void cursor_init(void)
{
    /* Pass 1: the arrow itself. */
    for (int y = 0; y < SPR_H; y++)
        for (int x = 0; x < SPR_W; x++)
            sprite[y * SPR_W + x] = art_at(x, y);

    /* Pass 2: a soft drop shadow - any empty pixel that touches the arrow
     * on its upper-left side picks up translucent black. Two strengths so
     * the shadow fades out; this is what makes the cursor look like it
     * floats over the screen instead of being pasted on it. */
    for (int y = 0; y < SPR_H; y++) {
        for (int x = 0; x < SPR_W; x++) {
            if (sprite[y * SPR_W + x])
                continue;
            if (art_at(x - 1, y - 1) || art_at(x - 1, y) || art_at(x, y - 1))
                sprite[y * SPR_W + x] = 0x58000000;     /* near shadow */
            else if (art_at(x - 2, y - 2) || art_at(x - 2, y - 1) ||
                     art_at(x - 1, y - 2))
                sprite[y * SPR_W + x] = 0x28000000;     /* fading edge */
        }
    }

    fb_set_cursor_sprite(sprite, SPR_W, SPR_H, 0, 0);
    fb_cursor_move((int)fb_width() / 2, (int)fb_height() / 2);
    fb_cursor_show(1);
}
