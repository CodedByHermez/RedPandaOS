#include "fb.h"

#include "font.h"
#include "heap.h"
#include "string.h"
#include "videoinfo.h"

static uint8_t *lfb;            /* the real screen */
static uint8_t *target;         /* where drawing goes (lfb or backbuffer) */
static uint8_t *backbuf;
static uint32_t pitch;
static uint32_t bypp;           /* bytes per pixel: 4 or 3 */
static int W, H;

/* Mouse cursor overlay state. */
static const uint32_t *cur_sprite;
static int cur_w, cur_h, cur_hx, cur_hy;
static volatile int cur_x, cur_y;
static int cur_visible;

/* Blend the cursor sprite over the given screen rectangle, reading
 * background pixels from the backbuffer and writing straight to the LFB.
 * Called after a region was flipped so the cursor always ends up on top. */
static void composite_cursor(int rx, int ry, int rw, int rh)
{
    if (!cur_visible || !cur_sprite || !backbuf)
        return;

    int sx = cur_x - cur_hx, sy = cur_y - cur_hy;
    int x0 = rx > sx ? rx : sx;
    int y0 = ry > sy ? ry : sy;
    int x1 = (rx + rw < sx + cur_w) ? rx + rw : sx + cur_w;
    int y1 = (ry + rh < sy + cur_h) ? ry + rh : sy + cur_h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            uint32_t s = cur_sprite[(y - sy) * cur_w + (x - sx)];
            uint32_t a = s >> 24;
            if (!a)
                continue;

            uint32_t off = (uint32_t)y * pitch + (uint32_t)x * bypp;
            uint32_t sr = (s >> 16) & 0xFF, sg = (s >> 8) & 0xFF, sb = s & 0xFF;
            uint8_t *dst = lfb + off;

            if (a == 255) {
                dst[0] = (uint8_t)sb;
                dst[1] = (uint8_t)sg;
                dst[2] = (uint8_t)sr;
            } else {
                const uint8_t *bg = backbuf + off;  /* what's "under" us */
                dst[0] = (uint8_t)((sb * a + bg[0] * (255 - a)) / 255);
                dst[1] = (uint8_t)((sg * a + bg[1] * (255 - a)) / 255);
                dst[2] = (uint8_t)((sr * a + bg[2] * (255 - a)) / 255);
            }
            if (bypp == 4)
                dst[3] = 0;
        }
    }
}

void fb_init(void)
{
    const video_info_t *vi = video_info();
    lfb    = (uint8_t *)vi->lfb;
    target = lfb;
    pitch  = vi->pitch;
    bypp   = vi->bpp / 8;
    W      = vi->width;
    H      = vi->height;
}

void fb_enable_backbuffer(void)
{
    backbuf = kmalloc(pitch * (uint32_t)H);
    if (!backbuf)
        return;                 /* no RAM? keep drawing directly */
    memcpy(backbuf, lfb, pitch * (uint32_t)H);
    target = backbuf;
}

uint32_t fb_width(void)  { return (uint32_t)W; }
uint32_t fb_height(void) { return (uint32_t)H; }

void fb_putpixel(int x, int y, uint32_t rgb)
{
    if (x < 0 || y < 0 || x >= W || y >= H)
        return;
    uint8_t *p = target + (uint32_t)y * pitch + (uint32_t)x * bypp;
    p[0] = (uint8_t)rgb;            /* blue  - VESA framebuffers are BGR */
    p[1] = (uint8_t)(rgb >> 8);     /* green */
    p[2] = (uint8_t)(rgb >> 16);    /* red */
    if (bypp == 4)
        p[3] = 0;
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t rgb)
{
    /* Clip to the screen. */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w <= 0 || h <= 0)
        return;

    for (int row = y; row < y + h; row++) {
        uint8_t *p = target + (uint32_t)row * pitch + (uint32_t)x * bypp;
        if (bypp == 4) {
            uint32_t *p32 = (uint32_t *)p;
            for (int i = 0; i < w; i++)
                p32[i] = rgb;
        } else {
            for (int i = 0; i < w; i++) {
                p[0] = (uint8_t)rgb;
                p[1] = (uint8_t)(rgb >> 8);
                p[2] = (uint8_t)(rgb >> 16);
                p += 3;
            }
        }
    }
}

void fb_draw_rect(int x, int y, int w, int h, uint32_t rgb)
{
    fb_fill_rect(x, y, w, 1, rgb);
    fb_fill_rect(x, y + h - 1, w, 1, rgb);
    fb_fill_rect(x, y, 1, h, rgb);
    fb_fill_rect(x + w - 1, y, 1, h, rgb);
}

void fb_line(int x0, int y0, int x1, int y1, uint32_t rgb)
{
    /* Bresenham, all octants. */
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        fb_putpixel(x0, y0, rgb);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void fb_text(int x, int y, const char *s, uint32_t rgb)
{
    for (; *s; s++, x += FONT_W) {
        const uint8_t *glyph = font_glyph((unsigned char)*s);
        for (int gy = 0; gy < FONT_H; gy++) {
            uint8_t bits = glyph[gy];
            for (int gx = 0; gx < FONT_W; gx++)
                if (bits & (0x80 >> gx))
                    fb_putpixel(x + gx, y + gy, rgb);
        }
    }
}

void fb_text_scaled(int x, int y, const char *s, uint32_t rgb, int scale)
{
    for (; *s; s++, x += FONT_W * scale) {
        const uint8_t *glyph = font_glyph((unsigned char)*s);
        for (int gy = 0; gy < FONT_H; gy++) {
            uint8_t bits = glyph[gy];
            for (int gx = 0; gx < FONT_W; gx++)
                if (bits & (0x80 >> gx))
                    fb_fill_rect(x + gx * scale, y + gy * scale,
                                 scale, scale, rgb);
        }
    }
}

void fb_blend_pixel(int x, int y, uint32_t rgb, uint8_t alpha)
{
    if (x < 0 || y < 0 || x >= W || y >= H || !alpha)
        return;
    uint8_t *p = target + (uint32_t)y * pitch + (uint32_t)x * bypp;
    uint32_t a = alpha, na = 255 - a;
    p[0] = (uint8_t)((( rgb        & 0xFF) * a + p[0] * na) / 255);
    p[1] = (uint8_t)((((rgb >>  8) & 0xFF) * a + p[1] * na) / 255);
    p[2] = (uint8_t)((((rgb >> 16) & 0xFF) * a + p[2] * na) / 255);
}

void fb_blend_rect(int x, int y, int w, int h, uint32_t rgb, uint8_t alpha)
{
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++)
            fb_blend_pixel(px, py, rgb, alpha);
}

static int isqrt(int v)
{
    int r = 0;
    while ((r + 1) * (r + 1) <= v)
        r++;
    return r;
}

void fb_fill_circle(int cx, int cy, int r, uint32_t rgb)
{
    for (int dy = -r; dy <= r; dy++) {
        int half = isqrt(r * r - dy * dy);
        fb_fill_rect(cx - half, cy + dy, half * 2 + 1, 1, rgb);
    }
}

int fb_round_inset(int d, int r)
{
    if (d >= r)
        return 0;
    int dy = r - d;
    return r - isqrt(r * r - dy * dy);
}

void fb_fill_rounded_rect(int x, int y, int w, int h, int r,
                          uint32_t rgb, int round_flags)
{
    for (int row = 0; row < h; row++) {
        int inset = 0;
        if ((round_flags & ROUND_TOP) && row < r)
            inset = fb_round_inset(row, r);
        else if ((round_flags & ROUND_BOTTOM) && row >= h - r)
            inset = fb_round_inset(h - 1 - row, r);
        fb_fill_rect(x + inset, y + row, w - 2 * inset, 1, rgb);
        if (inset > 0) {        /* soften the staircase edge */
            fb_blend_pixel(x + inset - 1, y + row, rgb, 110);
            fb_blend_pixel(x + w - inset, y + row, rgb, 110);
        }
    }
}

void fb_snapshot(void *dst)
{
    memcpy(dst, backbuf ? backbuf : lfb, pitch * (uint32_t)H);
}

void fb_restore(const void *src)
{
    if (backbuf)
        memcpy(backbuf, src, pitch * (uint32_t)H);
}

uint32_t fb_buffer_size(void)
{
    return pitch * (uint32_t)H;
}

void fb_flip(void)
{
    if (!backbuf)
        return;
    memcpy(lfb, backbuf, pitch * (uint32_t)H);
    composite_cursor(0, 0, W, H);
}

void fb_flip_rect(int x, int y, int w, int h)
{
    if (!backbuf)
        return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w <= 0 || h <= 0)
        return;

    for (int row = y; row < y + h; row++) {
        uint32_t off = (uint32_t)row * pitch + (uint32_t)x * bypp;
        memcpy(lfb + off, backbuf + off, (uint32_t)w * bypp);
    }
    composite_cursor(x, y, w, h);
}

void fb_set_cursor_sprite(const uint32_t *argb, int w, int h,
                          int hot_x, int hot_y)
{
    cur_sprite = argb;
    cur_w = w;
    cur_h = h;
    cur_hx = hot_x;
    cur_hy = hot_y;
}

void fb_cursor_show(int visible)
{
    cur_visible = visible;
    fb_flip_rect(cur_x - cur_hx, cur_y - cur_hy, cur_w, cur_h);
}

void fb_cursor_move(int x, int y)
{
    int ox = cur_x - cur_hx, oy = cur_y - cur_hy;
    cur_x = x;
    cur_y = y;
    /* Repaint where it was (composite no longer covers it -> clean
     * background restore) and where it is now. Two tiny rects, no flicker. */
    fb_flip_rect(ox, oy, cur_w, cur_h);
    fb_flip_rect(x - cur_hx, y - cur_hy, cur_w, cur_h);
}

void fb_scroll_up(int pixels, uint32_t fill_rgb)
{
    /* Row by row, top down: each row copy is non-overlapping. */
    for (int y = 0; y < H - pixels; y++)
        memcpy(target + (uint32_t)y * pitch,
               target + (uint32_t)(y + pixels) * pitch,
               (uint32_t)W * bypp);
    fb_fill_rect(0, H - pixels, W, pixels, fill_rgb);
    fb_flip();
}
