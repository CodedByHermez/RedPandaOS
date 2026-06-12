/* =============================================================================
 * RedPandaOS - text console on the framebuffer
 * =============================================================================
 * Replaces the VGA text-mode driver from v2-v4: same idea (a grid of
 * characters with 16 colors), but every glyph is now rendered pixel by
 * pixel from the bitmap font. The 16 colors keep their classic VGA names
 * and are translated to RGB.
 * ============================================================================= */

#ifndef CONSOLE_H
#define CONSOLE_H

typedef enum {
    COLOR_BLACK         = 0,
    COLOR_BLUE          = 1,
    COLOR_GREEN         = 2,
    COLOR_CYAN          = 3,
    COLOR_RED           = 4,
    COLOR_MAGENTA       = 5,
    COLOR_BROWN         = 6,
    COLOR_LIGHT_GREY    = 7,
    COLOR_DARK_GREY     = 8,
    COLOR_LIGHT_BLUE    = 9,
    COLOR_LIGHT_GREEN   = 10,
    COLOR_LIGHT_CYAN    = 11,
    COLOR_LIGHT_RED     = 12,
    COLOR_LIGHT_MAGENTA = 13,
    COLOR_YELLOW        = 14,
    COLOR_WHITE         = 15,
} console_color_t;

void console_init(void);
void console_clear(void);
void console_set_color(console_color_t fg, console_color_t bg);
void console_putc(char c);
void console_print(const char *s);

#endif
