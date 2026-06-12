#include "console.h"

#include "fb.h"
#include "font.h"

#define TAB_WIDTH 4

/* The classic VGA 16-color palette, as RGB. */
static const uint32_t palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

static int cols, rows;
static int col, row;
static uint32_t fg, bg;

static void draw_glyph(int r, int c, char ch)
{
    const uint8_t *glyph = font_glyph((unsigned char)ch);
    int px = c * FONT_W, py = r * FONT_H;

    for (int gy = 0; gy < FONT_H; gy++) {
        uint8_t bits = glyph[gy];
        for (int gx = 0; gx < FONT_W; gx++)
            fb_putpixel(px + gx, py + gy, (bits & (0x80 >> gx)) ? fg : bg);
    }
    fb_flip_rect(px, py, FONT_W, FONT_H);
}

static void fill_cell(int r, int c)
{
    fb_fill_rect(c * FONT_W, r * FONT_H, FONT_W, FONT_H, bg);
    fb_flip_rect(c * FONT_W, r * FONT_H, FONT_W, FONT_H);
}

/* The cursor is a thin bar at the baseline of the current cell. */
static void cursor_draw(void)
{
    fb_fill_rect(col * FONT_W, row * FONT_H + FONT_H - 2, FONT_W, 2, fg);
    fb_flip_rect(col * FONT_W, row * FONT_H, FONT_W, FONT_H);
}

static void cursor_erase(void)
{
    fill_cell(row, col);
}

void console_set_color(console_color_t f, console_color_t b)
{
    fg = palette[f & 15];
    bg = palette[b & 15];
}

void console_clear(void)
{
    fb_fill_rect(0, 0, (int)fb_width(), (int)fb_height(), bg);
    col = 0;
    row = 0;
    fb_flip();
    cursor_draw();
}

void console_init(void)
{
    cols = (int)fb_width() / FONT_W;
    rows = (int)fb_height() / FONT_H;
    console_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    console_clear();
}

void console_putc(char c)
{
    cursor_erase();

    switch (c) {
    case '\n':
        col = 0;
        row++;
        break;
    case '\r':
        col = 0;
        break;
    case '\b':
        if (col > 0) {
            col--;
            fill_cell(row, col);
        }
        break;
    case '\t':
        col = (col + TAB_WIDTH) & ~(TAB_WIDTH - 1);
        break;
    default:
        draw_glyph(row, col, c);
        col++;
        break;
    }

    if (col >= cols) {          /* wrap long lines */
        col = 0;
        row++;
    }
    if (row >= rows) {
        fb_scroll_up(FONT_H, bg);
        row = rows - 1;
    }

    cursor_draw();
}

void console_print(const char *s)
{
    while (*s)
        console_putc(*s++);
}
