/* =============================================================================
 * RedPandaOS - framebuffer driver
 * =============================================================================
 * Pixel-level drawing on the VBE linear framebuffer. Colors are 0xRRGGBB.
 *
 * All drawing goes to a target buffer. Before the heap exists that is the
 * framebuffer itself (pixels appear immediately, flips are no-ops); after
 * fb_enable_backbuffer() everything is drawn off-screen and only fb_flip /
 * fb_flip_rect copy it to the screen - no flicker.
 * ============================================================================= */

#ifndef FB_H
#define FB_H

#include <stdint.h>

void fb_init(void);
void fb_enable_backbuffer(void);    /* call once the heap is up */

uint32_t fb_width(void);
uint32_t fb_height(void);

void fb_putpixel(int x, int y, uint32_t rgb);
void fb_fill_rect(int x, int y, int w, int h, uint32_t rgb);
void fb_draw_rect(int x, int y, int w, int h, uint32_t rgb);   /* outline */
void fb_line(int x0, int y0, int x1, int y1, uint32_t rgb);
void fb_text(int x, int y, const char *s, uint32_t rgb);       /* transparent bg */
void fb_text_scaled(int x, int y, const char *s, uint32_t rgb, int scale);

/* --- compositor primitives (GUI) ----------------------------------------- */
void fb_blend_pixel(int x, int y, uint32_t rgb, uint8_t alpha);
void fb_blend_rect(int x, int y, int w, int h, uint32_t rgb, uint8_t alpha);
void fb_fill_circle(int cx, int cy, int r, uint32_t rgb);

#define ROUND_TOP    1
#define ROUND_BOTTOM 2
void fb_fill_rounded_rect(int x, int y, int w, int h, int r,
                          uint32_t rgb, int round_flags);
int  fb_round_inset(int d, int r);   /* horizontal inset d rows into a corner */

/* Save/restore the whole backbuffer - used to cache the desktop wallpaper
 * so each GUI frame starts from a finished background in one memcpy. */
void fb_snapshot(void *dst);
void fb_restore(const void *src);
uint32_t fb_buffer_size(void);

void fb_flip(void);                          /* whole screen */
void fb_flip_rect(int x, int y, int w, int h);
void fb_scroll_up(int pixels, uint32_t fill_rgb);   /* incl. full flip */

/* --- Hardware-cursor-style mouse pointer --------------------------------
 * The sprite (ARGB, straight alpha) is composited over the framebuffer at
 * flip time: it is always on top, drawing can never erase it, and moving
 * it only repaints two small rectangles - that is what makes it smooth. */
void fb_set_cursor_sprite(const uint32_t *argb, int w, int h,
                          int hot_x, int hot_y);
void fb_cursor_show(int visible);
void fb_cursor_move(int x, int y);           /* hotspot in screen coords */

#endif
