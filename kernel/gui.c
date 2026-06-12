/* =============================================================================
 * RedPandaOS - the GUI: desktop, icons, trash, start menu, window manager
 * =============================================================================
 * An immediate-mode compositor:
 *   - The wallpaper (gradient + glow blobs) is rendered ONCE into a cached
 *     background layer on the heap.
 *   - Every frame: restore the background in one memcpy, draw the desktop
 *     icons, draw the windows bottom-to-top (shadow, rounded frame, title
 *     bar, content), draw the translucent taskbar, then the start menu and
 *     drag ghosts on top, flip. The mouse cursor rides above it all via the
 *     fb cursor compositing from v6.
 *   - The event loop drains the whole input queue per frame, so 200 Hz
 *     mouse floods coalesce into one redraw - that is what keeps dragging
 *     smooth.
 *
 * The desktop is dynamic (v10): icons and the trash can be dropped anywhere,
 * and the arrangement persists in a hidden layout file on RPFS. The paw orb
 * opens the start menu: apps, About, Restart, Shut Down.
 * ============================================================================= */

#include "gui.h"

#include "fb.h"
#include "font.h"
#include "fs.h"
#include "heap.h"
#include "input.h"
#include "io.h"
#include "pmm.h"
#include "rtc.h"
#include "string.h"
#include "timer.h"

#define TASKBAR_H   36
#define TITLE_H     30
#define WIN_RADIUS  8
#define SHADOW      14
#define MAX_WINDOWS 8
#define TITLE_MAX   23

/* palette */
#define COL_BODY        0x1E1E28
#define COL_TITLE_TOP   0x3A3A4E
#define COL_TITLE_BOT   0x232330
#define COL_TITLE_TOP_U 0x282830
#define COL_TITLE_BOT_U 0x1F1F26
#define COL_TEXT        0xC8C8D8
#define COL_TEXT_DIM    0x8A8AA0
#define COL_ACCENT      0xE8743B    /* red panda orange */
#define COL_CLOSE       0xFF5F57
#define COL_MINI        0xFEBC2E
#define COL_ZOOM        0x28C840
#define COL_ICON_PAGE   0xECECF4
#define COL_ICON_FOLD   0xB8BCD0
#define COL_ICON_RULE   0x9AA0B8
#define COL_LABEL       0xD8DCE8

/* desktop icons */
#define ICON_CELL_W     96
#define ICON_CELL_H     92
#define ICON_MARGIN     16
#define TRASH_CH        0x01    /* filename prefix: "in the trash" */
#define HIDDEN_CH       0x02    /* filename prefix: system files (layout) */
#define LAYOUT_FILE     "\x02layout"
#define DBLCLICK_TICKS  50      /* 0.5 s at 100 Hz */

/* the start menu */
#define MENU_X      10
#define MENU_W      248
#define MENU_H      344
#define MENU_RADIUS 10
#define ROW_H       32

typedef struct window window_t;
struct window {
    int x, y, w, h;
    char title[TITLE_MAX + 1];
    void (*draw)(window_t *self);
    void (*click)(window_t *self, int mx, int my);  /* content-area clicks */
    void (*key)(window_t *self, uint8_t ch);        /* focused keyboard input */
    void (*on_close)(window_t *self);               /* free app state */
    void *user;             /* app state (the editor), owned via on_close */
    int visible;
    int used;
};

static window_t wins[MAX_WINDOWS];
static int zorder[MAX_WINDOWS];     /* indexes into wins[], bottom -> top */
static int zcount;
static int focused = -1;

static uint8_t *bg_layer;           /* cached finished wallpaper */
static int scr_w, scr_h;
static int dragging = -1;           /* wins index being dragged, or -1 */
static int drag_dx, drag_dy;        /* grab offset inside the title bar */
static int running;

/* --- desktop state -------------------------------------------------------- */

typedef struct {
    char name[FS_MAX_NAME + 1];
    uint32_t size;
    int x, y;                       /* cell top-left; -1 = not placed yet */
    int selected;
} icon_t;

static icon_t icons[FS_DIR_ENTRIES];
static int icon_count;
static int trash_x, trash_y;        /* trash cell top-left */

/* Saved icon positions, one record per file ("\x02trash" = the trash). */
typedef struct {
    char name[FS_MAX_NAME + 1];
    int16_t x, y;
} __attribute__((packed)) layout_rec_t;

static layout_rec_t layout[FS_DIR_ENTRIES + 1];
static int layout_n;

static int band_on;                 /* rubber-band selection active */
static int band_ax, band_ay;        /* anchor (where the press happened) */
static int band_x, band_y;          /* current corner (the mouse) */

static int icon_armed = -1;         /* icon pressed, drag not started yet */
static int icon_dragging;           /* drop preview follows the mouse */
static int trash_armed;             /* same pair for the trash itself */
static int trash_dragging;
static int trash_grab_dx, trash_grab_dy;
static int press_x, press_y;

static int mouse_x, mouse_y;        /* last seen cursor position */
static int click_target = -99;      /* double-click tracking: icon idx, -2 = trash */
static uint32_t click_tick;

static int menu_open;
static int menu_y;                  /* fixed: above the taskbar */
static int menu_slide;              /* extra y offset during the open animation */

/* --- tiny string builders (window content writes raw text to pixels) ---- */

static char *app_str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

static char *app_int(char *p, uint32_t v)
{
    char tmp[12];
    int i = 0;
    do {
        tmp[i++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (i--)
        *p++ = tmp[i];
    return p;
}

static char *app_2digit(char *p, int v)
{
    *p++ = (char)('0' + v / 10);
    *p++ = (char)('0' + v % 10);
    return p;
}

/* Crisp text with a 1px dark offset behind it - readable on anything. */
static void text_shadowed(int x, int y, const char *s, uint32_t rgb)
{
    fb_text(x + 1, y + 1, s, 0x101014);
    fb_text(x, y, s, rgb);
}

static void wait_ticks(uint32_t n)
{
    uint32_t t0 = timer_ticks();
    while (timer_ticks() - t0 < n)
        __asm__ __volatile__("hlt");
}

/* --- wallpaper ------------------------------------------------------------ */

static void glow(int cx, int cy, int r, uint32_t rgb, int max_alpha)
{
    int r2 = r * r;
    for (int y = cy - r; y <= cy + r; y++) {
        if (y < 0 || y >= scr_h)
            continue;
        for (int x = cx - r; x <= cx + r; x++) {
            if (x < 0 || x >= scr_w)
                continue;
            int dx = x - cx, dy = y - cy;
            int d2 = dx * dx + dy * dy;
            if (d2 >= r2)
                continue;
            /* quadratic falloff: bright core, soft edge */
            int t = 255 - (d2 * 255) / r2;
            int a = (max_alpha * t * t) / (255 * 255);
            fb_blend_pixel(x, y, rgb, (uint8_t)a);
        }
    }
}

static uint32_t lerp_rgb(uint32_t a, uint32_t b, int t, int max)
{
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = ar + (br - ar) * t / max;
    int g = ag + (bg - ag) * t / max;
    int bl = ab + (bb - ab) * t / max;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

static void render_wallpaper(void)
{
    /* deep teal gradient, dark at the top */
    for (int y = 0; y < scr_h; y++)
        fb_fill_rect(0, y, scr_w, 1, lerp_rgb(0x0F2027, 0x2C5364, y, scr_h - 1));

    /* soft colored glows - the "abstract wallpaper" look */
    glow(scr_w * 78 / 100, scr_h * 16 / 100, 300, COL_ACCENT, 80);
    glow(scr_w * 10 / 100, scr_h * 88 / 100, 340, 0x7B2FBE, 65);
    glow(scr_w * 46 / 100, scr_h * 62 / 100, 260, 0x2EC4B6, 45);

    /* quiet branding, bottom right above the taskbar */
    const char *brand = "RedPandaOS 1.0";
    int bx = scr_w - (int)strlen(brand) * FONT_W - 18;
    int by = scr_h - TASKBAR_H - 30;
    fb_text(bx + 1, by + 1, brand, 0x11202A);   /* soft depth */
    fb_text(bx, by, brand, 0x55758A);
}

/* --- window chrome --------------------------------------------------------- */

/* Shadow that hugs the ROUNDED window outline: measure each pixel's
 * distance to the rounded body (distance to the body's inner "core" rect,
 * minus the corner radius), so the shadow wraps the corners instead of
 * leaving bare square notches there. */
static void window_shadow(int x, int y, int w, int h)
{
    int cx0 = x + WIN_RADIUS, cy0 = y + WIN_RADIUS;             /* core rect */
    int cx1 = x + w - 1 - WIN_RADIUS, cy1 = y + h - 1 - WIN_RADIUS;

    for (int py = y - SHADOW; py < y + h + SHADOW; py++) {
        for (int px = x - SHADOW; px < x + w + SHADOW; px++) {
            int qx = px < cx0 ? cx0 : (px > cx1 ? cx1 : px);
            int qy = py < cy0 ? cy0 : (py > cy1 ? cy1 : py);
            int dx = px - qx, dy = py - qy;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            int hi = dx > dy ? dx : dy;
            int lo = dx > dy ? dy : dx;
            int d = hi + lo / 2 - WIN_RADIUS;   /* octagonal dist to body edge */
            if (d < 0)
                continue;                       /* inside the body */
            if (d >= SHADOW)
                continue;
            int t = SHADOW - d;
            fb_blend_pixel(px, py, 0x000000, (uint8_t)(t * t * 110 / (SHADOW * SHADOW)));
        }
    }
}

static void draw_window(window_t *win, int is_focused)
{
    int x = win->x, y = win->y, w = win->w, h = win->h;

    window_shadow(x, y, w, h);

    /* body with rounded corners */
    fb_fill_rounded_rect(x, y, w, h, WIN_RADIUS, COL_BODY,
                         ROUND_TOP | ROUND_BOTTOM);

    /* title bar: vertical gradient, rounded only at the top */
    uint32_t top = is_focused ? COL_TITLE_TOP : COL_TITLE_TOP_U;
    uint32_t bot = is_focused ? COL_TITLE_BOT : COL_TITLE_BOT_U;
    for (int ty = 0; ty < TITLE_H; ty++) {
        int inset = ty < WIN_RADIUS ? fb_round_inset(ty, WIN_RADIUS) : 0;
        fb_fill_rect(x + inset, y + ty, w - 2 * inset, 1,
                     lerp_rgb(top, bot, ty, TITLE_H - 1));
    }
    fb_fill_rect(x, y + TITLE_H, w, 1, 0x14141A);   /* separator */

    /* traffic lights */
    int cy = y + TITLE_H / 2;
    fb_fill_circle(x + 20, cy, 6, COL_CLOSE);
    fb_fill_circle(x + 40, cy, 6, COL_MINI);
    fb_fill_circle(x + 60, cy, 6, COL_ZOOM);
    if (is_focused) {           /* glyphs appear on the focused window */
        fb_line(x + 17, cy - 3, x + 23, cy + 3, 0x7A1815);
        fb_line(x + 17, cy + 3, x + 23, cy - 3, 0x7A1815);
        fb_fill_rect(x + 37, cy, 7, 2, 0x8A5D0A);
    }

    /* centered title */
    int tlen = (int)strlen(win->title) * FONT_W;
    int tx = x + (w - tlen) / 2;
    if (tx < x + 76)
        tx = x + 76;
    text_shadowed(tx, y + (TITLE_H - FONT_H) / 2,
                  win->title, is_focused ? 0xEAEAF2 : 0x9090A0);

    /* hairline border, brighter when focused */
    uint8_t ba = is_focused ? 70 : 30;
    fb_blend_rect(x + WIN_RADIUS, y, w - 2 * WIN_RADIUS, 1, 0xFFFFFF, ba);
    fb_blend_rect(x + WIN_RADIUS, y + h - 1, w - 2 * WIN_RADIUS, 1, 0xFFFFFF, ba / 2);
    fb_blend_rect(x, y + WIN_RADIUS, 1, h - 2 * WIN_RADIUS, 0xFFFFFF, ba / 2);
    fb_blend_rect(x + w - 1, y + WIN_RADIUS, 1, h - 2 * WIN_RADIUS, 0xFFFFFF, ba / 2);

    if (win->draw)
        win->draw(win);
}

/* --- the built-in apps --------------------------------------------------- */

static void draw_panda(int ox, int oy, int s)
{
    const uint32_t FUR = 0xC65A1E, DARK = 0x6E3208, CREAM = 0xF2E0C8, INK = 0x1A1A1A;
    fb_fill_rect(ox + 1*s,  oy,       3*s, 3*s, DARK);
    fb_fill_rect(ox + 12*s, oy,       3*s, 3*s, DARK);
    fb_fill_rect(ox + 2*s,  oy + 1*s, 1*s, 1*s, CREAM);
    fb_fill_rect(ox + 13*s, oy + 1*s, 1*s, 1*s, CREAM);
    fb_fill_rect(ox,        oy + 2*s, 16*s, 12*s, FUR);
    fb_fill_rect(ox + 2*s,  oy + 5*s, 4*s, 3*s, CREAM);
    fb_fill_rect(ox + 10*s, oy + 5*s, 4*s, 3*s, CREAM);
    fb_fill_rect(ox + 3*s,  oy + 6*s, 2*s, 2*s, INK);
    fb_fill_rect(ox + 11*s, oy + 6*s, 2*s, 2*s, INK);
    fb_fill_rect(ox + 5*s,  oy + 9*s, 6*s, 5*s, CREAM);
    fb_fill_rect(ox + 7*s,  oy + 10*s, 2*s, 2*s, INK);
    fb_fill_rect(ox,        oy + 9*s, 2*s, 3*s, CREAM);
    fb_fill_rect(ox + 14*s, oy + 9*s, 2*s, 3*s, CREAM);
}

static void welcome_draw(window_t *win)
{
    int x = win->x + 24, y = win->y + TITLE_H + 20;

    fb_text_scaled(x, y, "Hello!", COL_ACCENT, 2);
    y += 44;
    fb_text(x, y, "This desktop is drawn entirely", COL_TEXT); y += 18;
    fb_text(x, y, "from scratch - every pixel by",  COL_TEXT); y += 18;
    fb_text(x, y, "our own kernel.",                COL_TEXT); y += 30;
    fb_text(x, y, "drag   the title bar moves me",  COL_TEXT_DIM); y += 18;
    fb_text(x, y, "o o    close and minimize",      COL_TEXT_DIM);

    draw_panda(win->x + win->w - 92, win->y + win->h - 90, 5);
}

static void sysinfo_draw(window_t *win)
{
    int x = win->x + 24, y = win->y + TITLE_H + 18;
    char buf[64], *p;

    p = app_str(buf, "display   ");
    p = app_int(p, fb_width());
    p = app_str(p, " x ");
    p = app_int(p, fb_height());
    *p = 0;
    fb_text(x, y, buf, COL_TEXT); y += 22;

    p = app_str(buf, "memory    ");
    p = app_int(p, pmm_free_kb() / 1024);
    p = app_str(p, " / ");
    p = app_int(p, pmm_total_kb() / 1024);
    p = app_str(p, " MB free");
    *p = 0;
    fb_text(x, y, buf, COL_TEXT); y += 22;

    p = app_str(buf, "heap      ");
    p = app_int(p, heap_used_bytes() / 1024);
    p = app_str(p, " / ");
    p = app_int(p, heap_mapped_bytes() / 1024);
    p = app_str(p, " KB used");
    *p = 0;
    fb_text(x, y, buf, COL_TEXT); y += 22;

    p = app_str(buf, "disk      ");
    p = app_int(p, fs_free_bytes() / 1024);
    p = app_str(p, " / ");
    p = app_int(p, fs_total_bytes() / 1024);
    p = app_str(p, " KB free, ");
    p = app_int(p, (uint32_t)fs_file_count());
    p = app_str(p, " files");
    *p = 0;
    fb_text(x, y, buf, COL_TEXT); y += 22;

    p = app_str(buf, "uptime    ");
    p = app_int(p, timer_ticks() / timer_hz());
    p = app_str(p, " s");
    *p = 0;
    fb_text(x, y, buf, COL_TEXT); y += 30;

    fb_text(x, y, "all of it ours.", COL_ACCENT);
}

static void about_draw(window_t *w)
{
    int cx = w->x + w->w / 2;
    int y = w->y + TITLE_H + 22;

    draw_panda(cx - 32, y, 4);
    y += 70;
    const char *t1 = "RedPandaOS";
    fb_text_scaled(cx - (int)strlen(t1) * FONT_W, y, t1, COL_ACCENT, 2);
    y += 38;
    const char *t2 = "version 11";
    fb_text(cx - (int)strlen(t2) * FONT_W / 2, y, t2, COL_TEXT_DIM);
    y += 28;
    const char *t3 = "own bootloader, own kernel,";
    fb_text(cx - (int)strlen(t3) * FONT_W / 2, y, t3, COL_TEXT);
    y += 18;
    const char *t4 = "own everything.";
    fb_text(cx - (int)strlen(t4) * FONT_W / 2, y, t4, COL_TEXT);
    y += 28;
    const char *t5 = "made from scratch with <3";
    fb_text(cx - (int)strlen(t5) * FONT_W / 2, y, t5, COL_TEXT_DIM);
}

/* --- window management ------------------------------------------------------ */

static void raise_window(int idx)
{
    int zi = 0;
    while (zi < zcount && zorder[zi] != idx)
        zi++;
    if (zi == zcount)
        return;
    for (; zi < zcount - 1; zi++)
        zorder[zi] = zorder[zi + 1];
    zorder[zcount - 1] = idx;
    focused = idx;
}

static void focus_topmost_visible(void)
{
    focused = -1;
    for (int zi = zcount - 1; zi >= 0; zi--) {
        if (wins[zorder[zi]].visible) {
            focused = zorder[zi];
            return;
        }
    }
}

static void close_window(int idx)
{
    if (wins[idx].on_close)
        wins[idx].on_close(&wins[idx]);
    wins[idx].used = 0;
    wins[idx].visible = 0;
    int zi = 0;
    while (zi < zcount && zorder[zi] != idx)
        zi++;
    for (; zi < zcount - 1; zi++)
        zorder[zi] = zorder[zi + 1];
    if (zi < zcount)
        zcount--;
    if (focused == idx)
        focus_topmost_visible();
}

static window_t *add_window(int x, int y, int w, int h, const char *title,
                            void (*draw)(window_t *))
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (wins[i].used)
            continue;
        window_t *win = &wins[i];
        memset(win, 0, sizeof(*win));
        win->x = x;
        win->y = y;
        win->w = w;
        win->h = h;
        int n = 0;
        while (title[n] && n < TITLE_MAX) {
            win->title[n] = title[n];
            n++;
        }
        win->title[n] = '\0';
        win->draw = draw;
        win->visible = 1;
        win->used = 1;
        zorder[zcount++] = i;
        focused = i;
        return win;
    }
    return 0;
}

static window_t *find_window(const char *title)
{
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (wins[i].used && !strcmp(wins[i].title, title))
            return &wins[i];
    return 0;
}

static void show_and_raise(window_t *w)
{
    w->visible = 1;
    raise_window((int)(w - wins));
}

/* Open a singleton app window: raise it if it exists, create it if not. */
static void open_app(const char *title, void (*draw)(window_t *),
                     int x, int y, int w, int h)
{
    window_t *win = find_window(title);
    if (!win)
        win = add_window(x, y, w, h, title, draw);
    if (win)
        show_and_raise(win);
}

/* --- desktop layout persistence ------------------------------------------------ */

static void load_layout(void)
{
    int sz = fs_read(LAYOUT_FILE, layout, sizeof(layout));
    layout_n = sz > 0 ? (int)((uint32_t)sz / sizeof(layout_rec_t)) : 0;
}

static const layout_rec_t *layout_find(const char *name)
{
    for (int i = 0; i < layout_n; i++)
        if (!strcmp(layout[i].name, name))
            return &layout[i];
    return 0;
}

static void save_layout(void)
{
    int n = 0;
    for (int i = 0; i < icon_count; i++) {
        memset(&layout[n], 0, sizeof(layout[n]));
        memcpy(layout[n].name, icons[i].name, FS_MAX_NAME + 1);
        layout[n].x = (int16_t)icons[i].x;
        layout[n].y = (int16_t)icons[i].y;
        n++;
    }
    memset(&layout[n], 0, sizeof(layout[n]));
    memcpy(layout[n].name, "\x02trash", 7);
    layout[n].x = (int16_t)trash_x;
    layout[n].y = (int16_t)trash_y;
    n++;
    layout_n = n;
    fs_write(LAYOUT_FILE, layout, (uint32_t)n * sizeof(layout_rec_t));
}

/* --- desktop icons ------------------------------------------------------------ */

static int trash_file_count(void)
{
    char name[FS_MAX_NAME + 1];
    uint32_t size;
    int n = 0;
    for (int i = 0; i < FS_DIR_ENTRIES; i++)
        if (fs_dir_get(i, name, &size) && name[0] == TRASH_CH)
            n++;
    return n;
}

/* Keep a cell fully on the desktop (glyph + label above the taskbar). */
static void clamp_cell(int *x, int *y)
{
    if (*x < 0)                      *x = 0;
    if (*x > scr_w - ICON_CELL_W)    *x = scr_w - ICON_CELL_W;
    if (*y < 0)                      *y = 0;
    if (*y > scr_h - TASKBAR_H - 72) *y = scr_h - TASKBAR_H - 72;
}

static int cell_is_free(int cx, int cy)
{
    if (cx < trash_x + ICON_CELL_W && cx + ICON_CELL_W > trash_x &&
        cy < trash_y + ICON_CELL_H && cy + ICON_CELL_H > trash_y)
        return 0;
    for (int i = 0; i < icon_count; i++) {
        if (icons[i].x < 0)
            continue;
        if (cx < icons[i].x + ICON_CELL_W && cx + ICON_CELL_W > icons[i].x &&
            cy < icons[i].y + ICON_CELL_H && cy + ICON_CELL_H > icons[i].y)
            return 0;
    }
    return 1;
}

/* New files land in the first free grid cell (column-major, like v9).
 * Deliberately does NOT save the layout: grid spots are re-derived every
 * boot, and saving here would let one transiently failed layout read
 * permanently overwrite the user's arrangement with the default grid.
 * Only explicit user drops persist positions. */
static void place_new_icons(void)
{
    int rows = (scr_h - TASKBAR_H - 2 * ICON_MARGIN) / ICON_CELL_H;
    int cols = (scr_w - 2 * ICON_MARGIN) / ICON_CELL_W;
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;

    for (int i = 0; i < icon_count; i++) {
        if (icons[i].x >= 0)
            continue;                   /* cell_is_free skips x < 0 (us) */
        for (int c = 0; c < cols && icons[i].x < 0; c++) {
            for (int r = 0; r < rows; r++) {
                int cx = ICON_MARGIN + c * ICON_CELL_W;
                int cy = ICON_MARGIN + r * ICON_CELL_H;
                if (cell_is_free(cx, cy)) {
                    icons[i].x = cx;
                    icons[i].y = cy;
                    break;
                }
            }
        }
        if (icons[i].x < 0) {           /* desktop is packed: stack top-left */
            icons[i].x = ICON_MARGIN;
            icons[i].y = ICON_MARGIN;
        }
    }
}

/* Rebuild the icon list from the filesystem, preserving the position and
 * selection of files that are already on the desktop; files we have never
 * seen take their saved layout spot, brand-new ones a free grid cell. */
static void refresh_icons(void)
{
    static icon_t fresh[FS_DIR_ENTRIES];
    char name[FS_MAX_NAME + 1];
    uint32_t size;
    int n = 0;

    load_layout();

    for (int i = 0; i < FS_DIR_ENTRIES; i++) {
        if (!fs_dir_get(i, name, &size) ||
            name[0] == TRASH_CH || name[0] == HIDDEN_CH)
            continue;
        icon_t *ic = &fresh[n];
        memcpy(ic->name, name, FS_MAX_NAME + 1);
        ic->size = size;
        ic->selected = 0;
        ic->x = -1;
        ic->y = -1;
        for (int o = 0; o < icon_count; o++) {
            if (!strcmp(icons[o].name, ic->name)) {
                ic->selected = icons[o].selected;
                ic->x = icons[o].x;     /* keep the live position */
                ic->y = icons[o].y;
                break;
            }
        }
        if (ic->x < 0) {
            const layout_rec_t *r = layout_find(ic->name);
            if (r) {
                ic->x = r->x;
                ic->y = r->y;
                clamp_cell(&ic->x, &ic->y);
            }
        }
        n++;
    }
    memcpy(icons, fresh, sizeof(icons));
    icon_count = n;

    if (!trash_dragging) {
        const layout_rec_t *r = layout_find("\x02trash");
        if (r) {
            trash_x = r->x;
            trash_y = r->y;
            clamp_cell(&trash_x, &trash_y);
        }
    }

    place_new_icons();
}

/* Moving to the trash = renaming to the hidden prefix. If an identically
 * named file is already in there, the rename fails and the icon stays -
 * honest, and rare enough. Names at the 21-char limit lose their last
 * character to make room for the prefix. */
static void trash_file(const char *name)
{
    char tn[FS_MAX_NAME + 1];
    int i = 0;
    tn[0] = TRASH_CH;
    while (name[i] && i < FS_MAX_NAME - 1) {
        tn[i + 1] = name[i];
        i++;
    }
    tn[i + 1] = '\0';
    fs_rename(name, tn);
}

static void restore_file(const char *trashed)
{
    fs_rename(trashed, trashed + 1);
}

static void clear_selection(void)
{
    for (int i = 0; i < icon_count; i++)
        icons[i].selected = 0;
}

static int icon_at(int mx, int my)
{
    for (int i = icon_count - 1; i >= 0; i--)   /* last drawn = on top */
        if (mx >= icons[i].x + 10 && mx < icons[i].x + ICON_CELL_W - 10 &&
            my >= icons[i].y && my < icons[i].y + ICON_CELL_H - 16)
            return i;
    return -1;
}

static int over_trash(int mx, int my)
{
    return mx >= trash_x && mx < trash_x + ICON_CELL_W &&
           my >= trash_y && my < trash_y + ICON_CELL_H;
}

static void update_band_selection(void)
{
    int x0 = band_ax < band_x ? band_ax : band_x;
    int y0 = band_ay < band_y ? band_ay : band_y;
    int x1 = band_ax > band_x ? band_ax : band_x;
    int y1 = band_ay > band_y ? band_ay : band_y;

    for (int i = 0; i < icon_count; i++) {
        /* the icon's "solid" part: page + label */
        int ix0 = icons[i].x + 20, iy0 = icons[i].y + 2;
        int ix1 = ix0 + 56, iy1 = iy0 + 66;
        icons[i].selected = (x0 <= ix1 && x1 >= ix0 && y0 <= iy1 && y1 >= iy0);
    }
}

/* A little document: white page, folded top-right corner, ruled lines.
 * Drawn with blends so the same routine paints the drag preview. */
static void draw_doc_glyph(int x, int y, uint8_t a)
{
    fb_blend_rect(x, y, 21, 38, COL_ICON_PAGE, a);          /* left, full height */
    fb_blend_rect(x + 21, y + 9, 9, 29, COL_ICON_PAGE, a);  /* right, below fold */
    for (int i = 0; i < 9; i++)                             /* the fold */
        fb_blend_rect(x + 21, y + i, i + 1, 1, COL_ICON_FOLD, a);
    for (int k = 0; k < 4; k++)                             /* "text" rules */
        fb_blend_rect(x + 5, y + 15 + k * 6, 20, 2, COL_ICON_RULE, a);
    fb_blend_rect(x + 1, y + 38, 30, 1, 0x000000, a / 3);   /* soft ground shadow */
}

static void draw_icon_label(int cx, int cy, const char *name, uint32_t rgb)
{
    char t[12];
    int n = 0;
    while (name[n] && n < 11) {
        t[n] = name[n];
        n++;
    }
    t[n] = '\0';
    text_shadowed(cx + (ICON_CELL_W - n * FONT_W) / 2, cy + 52, t, rgb);
}

static void draw_trash_icon(int hot)
{
    int x = trash_x + 30, y = trash_y + 6;
    uint32_t body = hot ? COL_ACCENT : 0x99A1B5;
    uint32_t dark = hot ? 0xA84E1E : 0x6E7588;

    if (trash_file_count() > 0) {       /* crumpled paper peeking out */
        fb_fill_rect(x + 7,  y - 1, 8, 6, 0xE8E8F0);
        fb_fill_rect(x + 19, y - 3, 9, 8, 0xD8D8E4);
    }
    fb_fill_rect(x + 13, y, 10, 3, dark);           /* handle */
    fb_fill_rect(x - 2, y + 3, 40, 5, body);        /* lid */
    for (int r = 0; r < 32; r++) {                  /* tapering can */
        int inset = r * 5 / 32;
        fb_fill_rect(x + 2 + inset, y + 10 + r, 32 - 2 * inset, 1, body);
    }
    for (int k = 0; k < 3; k++)                     /* ridges */
        fb_fill_rect(x + 9 + k * 8, y + 14, 2, 24, dark);

    text_shadowed(trash_x + (ICON_CELL_W - 5 * FONT_W) / 2, trash_y + 52,
                  "Trash", COL_LABEL);
}

static void draw_desktop_icons(void)
{
    for (int i = 0; i < icon_count; i++) {
        icon_t *ic = &icons[i];
        if (ic->selected) {
            fb_blend_rect(ic->x + 6, ic->y, ICON_CELL_W - 12, ICON_CELL_H - 8,
                          COL_ACCENT, 45);
            fb_blend_rect(ic->x + 6, ic->y, ICON_CELL_W - 12, 1, COL_ACCENT, 150);
            fb_blend_rect(ic->x + 6, ic->y + ICON_CELL_H - 9, ICON_CELL_W - 12, 1,
                          COL_ACCENT, 150);
            fb_blend_rect(ic->x + 6, ic->y, 1, ICON_CELL_H - 8, COL_ACCENT, 150);
            fb_blend_rect(ic->x + ICON_CELL_W - 7, ic->y, 1, ICON_CELL_H - 8,
                          COL_ACCENT, 150);
        }
        draw_doc_glyph(ic->x + 33, ic->y + 6, 255);
        draw_icon_label(ic->x, ic->y, ic->name,
                        ic->selected ? 0xFFFFFF : COL_LABEL);
    }
    draw_trash_icon(icon_dragging && over_trash(mouse_x, mouse_y));
}

static void draw_band(void)
{
    int x0 = band_ax < band_x ? band_ax : band_x;
    int y0 = band_ay < band_y ? band_ay : band_y;
    int x1 = band_ax > band_x ? band_ax : band_x;
    int y1 = band_ay > band_y ? band_ay : band_y;
    int w = x1 - x0 + 1, h = y1 - y0 + 1;

    fb_blend_rect(x0, y0, w, h, COL_ACCENT, 30);
    fb_blend_rect(x0, y0, w, 1, COL_ACCENT, 160);
    fb_blend_rect(x0, y1, w, 1, COL_ACCENT, 160);
    fb_blend_rect(x0, y0, 1, h, COL_ACCENT, 160);
    fb_blend_rect(x1, y0, 1, h, COL_ACCENT, 160);
}

/* Drop preview: every selected icon, translucent, at where it would land. */
static void draw_drag_ghost(void)
{
    int dx = mouse_x - press_x, dy = mouse_y - press_y;
    for (int i = 0; i < icon_count; i++) {
        if (!icons[i].selected)
            continue;
        int gx = icons[i].x + dx, gy = icons[i].y + dy;
        clamp_cell(&gx, &gy);
        draw_doc_glyph(gx + 33, gy + 6, 150);
    }
}

/* =============================================================================
 * The text editor (double-click a desktop icon, or start menu > Text Editor)
 * =============================================================================
 * A flat byte buffer with a cursor: typing inserts, arrows navigate (up and
 * down remember the preferred column), clicking places the caret, Ctrl+S
 * saves to RPFS, Ctrl+Z / Ctrl+Y undo and redo. Undo is snapshot-based:
 * before every edit "burst" the whole buffer is copied onto a bounded stack;
 * consecutive same-kind edits within 1.5 s coalesce into one undo step, so
 * Ctrl+Z takes back a typed word, not one letter.
 * ============================================================================= */

#define ED_LINE_H   18
#define ED_PAD      14
#define ED_STATUS_H 28
#define ED_MAX_UNDO 24
#define ED_MAX_LEN  (32 * 1024)

typedef struct {
    char *buf;
    int len, cap;
    int cursor;                 /* byte offset */
    int scroll_line, scroll_col;
    int prefcol;                /* preferred column for up/down */
    int modified;
    char filename[FS_MAX_NAME + 1];
    struct ed_snap { char *buf; int len; int cursor; }
        undo[ED_MAX_UNDO], redo[ED_MAX_UNDO];
    int undo_n, redo_n;
    uint32_t last_edit_tick;
    int last_edit_kind;         /* 1 = insert, 2 = delete, 0 = barrier */
} editor_t;

/* --- buffer geometry ------------------------------------------------------ */

static void ed_linecol(editor_t *e, int pos, int *line, int *col)
{
    int l = 0, c = 0;
    for (int i = 0; i < pos; i++) {
        if (e->buf[i] == '\n') {
            l++;
            c = 0;
        } else {
            c++;
        }
    }
    *line = l;
    *col = c;
}

static int ed_line_start(editor_t *e, int line)
{
    int i = 0;
    while (line > 0 && i < e->len) {
        if (e->buf[i] == '\n')
            line--;
        i++;
    }
    return i;
}

static int ed_line_len(editor_t *e, int start)
{
    int n = 0;
    while (start + n < e->len && e->buf[start + n] != '\n')
        n++;
    return n;
}

static int ed_line_count(editor_t *e)
{
    int n = 1;
    for (int i = 0; i < e->len; i++)
        if (e->buf[i] == '\n')
            n++;
    return n;
}

static void ed_sync_prefcol(editor_t *e)
{
    int line;
    ed_linecol(e, e->cursor, &line, &e->prefcol);
}

/* --- undo / redo ----------------------------------------------------------- */

static void ed_clear_stack(struct ed_snap *st, int *n)
{
    for (int i = 0; i < *n; i++)
        kfree(st[i].buf);
    *n = 0;
}

static void ed_push_state(struct ed_snap *st, int *n, editor_t *e)
{
    char *copy = kmalloc(e->len ? (uint32_t)e->len : 1);
    if (!copy)
        return;
    memcpy(copy, e->buf, (uint32_t)e->len);
    if (*n == ED_MAX_UNDO) {            /* full: forget the oldest step */
        kfree(st[0].buf);
        memmove(&st[0], &st[1], sizeof(st[0]) * (ED_MAX_UNDO - 1));
        (*n)--;
    }
    st[*n].buf = copy;
    st[*n].len = e->len;
    st[*n].cursor = e->cursor;
    (*n)++;
}

static void ed_restore(editor_t *e, struct ed_snap *s)
{
    kfree(e->buf);                      /* snapshot becomes the live buffer */
    e->buf = s->buf;
    e->len = s->len;
    e->cap = s->len ? s->len : 1;
    e->cursor = s->cursor;
    e->modified = 1;
    e->last_edit_kind = 0;
    ed_sync_prefcol(e);
}

static void ed_before_edit(editor_t *e, int kind)
{
    uint32_t now = timer_ticks();
    int coalesce = (kind == e->last_edit_kind && e->undo_n &&
                    now - e->last_edit_tick < 150);
    if (!coalesce)
        ed_push_state(e->undo, &e->undo_n, e);
    e->last_edit_tick = now;
    e->last_edit_kind = kind;
    ed_clear_stack(e->redo, &e->redo_n);
    e->modified = 1;
}

static void ed_undo(editor_t *e)
{
    if (!e->undo_n)
        return;
    ed_push_state(e->redo, &e->redo_n, e);
    ed_restore(e, &e->undo[--e->undo_n]);
}

static void ed_redo(editor_t *e)
{
    if (!e->redo_n)
        return;
    ed_push_state(e->undo, &e->undo_n, e);
    ed_restore(e, &e->redo[--e->redo_n]);
}

/* --- editing --------------------------------------------------------------- */

static void ed_insert(editor_t *e, char c)
{
    if (e->len >= ED_MAX_LEN)
        return;
    ed_before_edit(e, 1);
    if (e->len + 1 > e->cap) {
        int newcap = e->cap < 256 ? 256 : e->cap * 2;
        char *nb = kmalloc((uint32_t)newcap);
        if (!nb)
            return;
        memcpy(nb, e->buf, (uint32_t)e->len);
        kfree(e->buf);
        e->buf = nb;
        e->cap = newcap;
    }
    memmove(e->buf + e->cursor + 1, e->buf + e->cursor,
            (uint32_t)(e->len - e->cursor));
    e->buf[e->cursor] = c;
    e->len++;
    e->cursor++;
    ed_sync_prefcol(e);
}

static void ed_delete_at(editor_t *e, int pos)
{
    if (pos < 0 || pos >= e->len)
        return;
    ed_before_edit(e, 2);
    memmove(e->buf + pos, e->buf + pos + 1, (uint32_t)(e->len - pos - 1));
    e->len--;
    e->cursor = pos;
    ed_sync_prefcol(e);
}

static void ed_vertical(editor_t *e, int dir)
{
    int line, col;
    ed_linecol(e, e->cursor, &line, &col);
    int target = line + dir;
    if (target < 0 || target >= ed_line_count(e))
        return;
    int start = ed_line_start(e, target);
    int ll = ed_line_len(e, start);
    e->cursor = start + (e->prefcol < ll ? e->prefcol : ll);
}

/* --- the editor window ------------------------------------------------------ */

static void ed_metrics(window_t *w, int *tx, int *ty, int *rows, int *cols)
{
    *tx = w->x + ED_PAD;
    *ty = w->y + TITLE_H + 10;
    *rows = (w->h - TITLE_H - 10 - ED_STATUS_H) / ED_LINE_H;
    *cols = (w->w - 2 * ED_PAD) / FONT_W;
}

static void ed_scroll_into_view(editor_t *e, int rows, int cols)
{
    int line, col;
    ed_linecol(e, e->cursor, &line, &col);
    if (line < e->scroll_line)         e->scroll_line = line;
    if (line >= e->scroll_line + rows) e->scroll_line = line - rows + 1;
    if (col < e->scroll_col)           e->scroll_col = col;
    if (col >= e->scroll_col + cols)   e->scroll_col = col - cols + 1;
}

static void editor_draw(window_t *w)
{
    editor_t *e = w->user;
    int tx, ty, rows, cols;
    ed_metrics(w, &tx, &ty, &rows, &cols);
    ed_scroll_into_view(e, rows, cols);

    /* the text, from scroll_line/scroll_col */
    char line[96];
    if (cols > 95)
        cols = 95;
    int i = ed_line_start(e, e->scroll_line);
    for (int r = 0; r < rows && i <= e->len; r++) {
        int ll = ed_line_len(e, i);
        int n = 0;
        for (int k = e->scroll_col; k < ll && n < cols; k++)
            line[n++] = e->buf[i + k];
        line[n] = '\0';
        fb_text(tx, ty + r * ED_LINE_H, line, COL_TEXT);
        i += ll + 1;                    /* past the newline */
    }

    /* the caret */
    int cl, cc;
    ed_linecol(e, e->cursor, &cl, &cc);
    if (cl >= e->scroll_line && cl < e->scroll_line + rows &&
        cc >= e->scroll_col && cc <= e->scroll_col + cols) {
        fb_fill_rect(tx + (cc - e->scroll_col) * FONT_W,
                     ty + (cl - e->scroll_line) * ED_LINE_H - 1,
                     2, FONT_H + 2, COL_ACCENT);
    }

    /* status bar */
    int sy = w->y + w->h - ED_STATUS_H;
    fb_fill_rect(w->x + 1, sy, w->w - 2, 1, 0x14141A);
    char st[48], *p = app_str(st, "Ln ");
    p = app_int(p, (uint32_t)(cl + 1));
    p = app_str(p, ", Col ");
    p = app_int(p, (uint32_t)(cc + 1));
    *p = 0;
    fb_text(w->x + ED_PAD, sy + 7, st, COL_TEXT_DIM);

    const char *hint = e->modified ? "modified - ^S saves" : "^S save  ^Z undo";
    fb_text(w->x + w->w - ED_PAD - (int)strlen(hint) * FONT_W, sy + 7,
            hint, e->modified ? COL_ACCENT : COL_TEXT_DIM);
}

static void editor_save(window_t *w)
{
    editor_t *e = w->user;
    if (fs_write(e->filename, e->buf, (uint32_t)e->len) == 0) {
        e->modified = 0;
        refresh_icons();                /* a new file appears on the desktop */
    }
}

static void editor_key(window_t *w, uint8_t ch)
{
    editor_t *e = w->user;
    int tx, ty, rows, cols, start, ll;
    ed_metrics(w, &tx, &ty, &rows, &cols);

    switch (ch) {
    case KEY_LEFT:
        if (e->cursor > 0)
            e->cursor--;
        ed_sync_prefcol(e);
        break;
    case KEY_RIGHT:
        if (e->cursor < e->len)
            e->cursor++;
        ed_sync_prefcol(e);
        break;
    case KEY_UP:
        ed_vertical(e, -1);
        break;
    case KEY_DOWN:
        ed_vertical(e, +1);
        break;
    case KEY_HOME: {
        int line, col;
        ed_linecol(e, e->cursor, &line, &col);
        e->cursor = ed_line_start(e, line);
        ed_sync_prefcol(e);
        break;
    }
    case KEY_END: {
        int line, col;
        ed_linecol(e, e->cursor, &line, &col);
        start = ed_line_start(e, line);
        ll = ed_line_len(e, start);
        e->cursor = start + ll;
        ed_sync_prefcol(e);
        break;
    }
    case KEY_DEL:
        ed_delete_at(e, e->cursor);
        break;
    case '\b':
        if (e->cursor > 0)
            ed_delete_at(e, e->cursor - 1);
        break;
    case '\n':
        ed_insert(e, '\n');
        break;
    case '\t':
        for (int k = 0; k < 4; k++)
            ed_insert(e, ' ');
        break;
    case 0x13:                          /* Ctrl+S */
        editor_save(w);
        break;
    case 0x1A:                          /* Ctrl+Z */
        ed_undo(e);
        break;
    case 0x19:                          /* Ctrl+Y */
        ed_redo(e);
        break;
    default:
        if (ch >= 32 && ch < 127)
            ed_insert(e, (char)ch);
        break;
    }
    ed_scroll_into_view(e, rows, cols);
}

static void editor_click(window_t *w, int mx, int my)
{
    editor_t *e = w->user;
    int tx, ty, rows, cols;
    ed_metrics(w, &tx, &ty, &rows, &cols);
    if (my >= w->y + w->h - ED_STATUS_H)
        return;

    int r = (my - ty) / ED_LINE_H;
    if (r < 0)
        r = 0;
    int target = e->scroll_line + r;
    int total = ed_line_count(e);
    if (target >= total)
        target = total - 1;
    int start = ed_line_start(e, target);
    int ll = ed_line_len(e, start);
    int c = (mx - tx + FONT_W / 2) / FONT_W + e->scroll_col;
    if (c < 0)
        c = 0;
    if (c > ll)
        c = ll;
    e->cursor = start + c;
    e->prefcol = c;
}

static void editor_close(window_t *w)
{
    editor_t *e = w->user;
    if (!e)
        return;
    ed_clear_stack(e->undo, &e->undo_n);
    ed_clear_stack(e->redo, &e->redo_n);
    kfree(e->buf);
    kfree(e);
    w->user = 0;
}

static window_t *find_editor(const char *name)
{
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (wins[i].used && wins[i].key == editor_key &&
            !strcmp(((editor_t *)wins[i].user)->filename, name))
            return &wins[i];
    return 0;
}

/* Open `name` in an editor window; loads the file if it exists, otherwise
 * starts an empty buffer (the file is created on the first Ctrl+S). */
static void open_editor(const char *name)
{
    window_t *w = find_editor(name);
    if (w) {
        show_and_raise(w);
        return;
    }

    editor_t *e = kmalloc(sizeof(editor_t));
    if (!e)
        return;
    memset(e, 0, sizeof(*e));
    int n = 0;
    while (name[n] && n < FS_MAX_NAME) {
        e->filename[n] = name[n];
        n++;
    }
    e->filename[n] = '\0';

    int size = fs_read(name, 0, 0);
    e->cap = size > 0 ? size + 64 : 256;
    e->buf = kmalloc((uint32_t)e->cap);
    if (!e->buf) {
        kfree(e);
        return;
    }
    if (size > 0) {
        fs_read(name, e->buf, (uint32_t)size);
        e->len = size;
    }

    static int stagger;
    w = add_window(170 + (stagger % 4) * 36, 80 + (stagger % 4) * 32,
                   560, 420, name, editor_draw);
    if (!w) {
        kfree(e->buf);
        kfree(e);
        return;
    }
    stagger++;
    w->user = e;
    w->key = editor_key;
    w->click = editor_click;
    w->on_close = editor_close;
}

/* Start menu > Text Editor: a fresh untitled file. */
static void open_new_file(void)
{
    char name[FS_MAX_NAME + 1];
    for (int i = 1; i < 100; i++) {
        char *p = app_str(name, "untitled");
        if (i > 1)
            p = app_int(p, (uint32_t)i);
        p = app_str(p, ".txt");
        *p = 0;
        if (fs_read(name, 0, 0) < 0 && !find_editor(name))
            break;
    }
    open_editor(name);
}

/* --- the trash window (double-click the trash) --------------------------------- */

#define TRASH_ROWS_MAX 6

static void trash_rows_origin(window_t *w, int *ox, int *oy)
{
    *ox = w->x + 22;
    *oy = w->y + TITLE_H + 12;
}

static void trash_win_draw(window_t *w)
{
    int x, y;
    trash_rows_origin(w, &x, &y);

    char name[FS_MAX_NAME + 1];
    uint32_t size;
    int rows = 0;
    for (int i = 0; i < FS_DIR_ENTRIES && rows < TRASH_ROWS_MAX; i++) {
        if (!fs_dir_get(i, name, &size) || name[0] != TRASH_CH)
            continue;
        fb_fill_rect(x, y + rows * 22 + 3, 8, 10, COL_ICON_PAGE);
        fb_text(x + 16, y + rows * 22, name + 1, COL_TEXT);
        char b[16], *p = app_int(b, size);
        p = app_str(p, " B");
        *p = 0;
        fb_text(w->x + w->w - 22 - (int)strlen(b) * FONT_W, y + rows * 22,
                b, COL_TEXT_DIM);
        rows++;
    }

    if (!rows) {
        fb_text(x, y + 8, "the trash is empty", COL_TEXT_DIM);
        return;
    }

    fb_text(x, w->y + w->h - 34, "click a file to restore", COL_TEXT_DIM);

    int bx = w->x + w->w - 126, by = w->y + w->h - 42;
    fb_fill_rounded_rect(bx, by, 104, 26, 6, COL_ACCENT, ROUND_TOP | ROUND_BOTTOM);
    fb_text(bx + (104 - 11 * FONT_W) / 2, by + 5, "Empty Trash", 0xFFFFFF);
}

static void trash_win_click(window_t *w, int mx, int my)
{
    char name[FS_MAX_NAME + 1];
    uint32_t size;

    /* the Empty Trash button */
    int bx = w->x + w->w - 126, by = w->y + w->h - 42;
    if (mx >= bx && mx < bx + 104 && my >= by && my < by + 26) {
        for (int i = 0; i < FS_DIR_ENTRIES; i++)
            if (fs_dir_get(i, name, &size) && name[0] == TRASH_CH)
                fs_delete(name);
        return;
    }

    /* a row = restore that file */
    int x, y;
    trash_rows_origin(w, &x, &y);
    if (my < y || mx < x || mx > w->x + w->w - 22)
        return;
    int row = (my - y) / 22;
    if (row >= TRASH_ROWS_MAX)
        return;
    int rows = 0;
    for (int i = 0; i < FS_DIR_ENTRIES; i++) {
        if (!fs_dir_get(i, name, &size) || name[0] != TRASH_CH)
            continue;
        if (rows == row) {
            restore_file(name);
            refresh_icons();
            return;
        }
        rows++;
    }
}

static void open_trash_window(void)
{
    window_t *w = find_window("Trash");
    if (!w)
        w = add_window(340, 220, 360, 230, "Trash", trash_win_draw);
    if (!w)
        return;
    w->click = trash_win_click;
    show_and_raise(w);
}

/* --- power ----------------------------------------------------------------- */

static void goodbye_screen(const char *msg)
{
    fb_fill_rect(0, 0, scr_w, scr_h, 0x0C0C12);
    draw_panda(scr_w / 2 - 40, scr_h / 2 - 100, 5);
    fb_text((scr_w - (int)strlen(msg) * FONT_W) / 2, scr_h / 2 + 10,
            msg, COL_TEXT);
    fb_flip();
}

static void system_restart(void)
{
    goodbye_screen("see you in a moment...");
    wait_ticks(70);

    /* pulse the CPU reset line via the 8042 keyboard controller */
    for (int i = 0; i < 100000; i++)
        if (!(inb(0x64) & 2))
            break;
    outb(0x64, 0xFE);
    wait_ticks(50);

    /* still alive? force a reset the rude way: triple fault */
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) nul = { 0, 0 };
    __asm__ __volatile__("lidt %0; int $3" : : "m"(nul));
    for (;;)
        __asm__ __volatile__("hlt");
}

static void system_shutdown(void)
{
    goodbye_screen("goodbye!");
    wait_ticks(80);

    outw(0x604,  0x2000);   /* QEMU (ACPI PM1a control) */
    outw(0xB004, 0x2000);   /* Bochs / older QEMU */
    outw(0x4004, 0x3400);   /* VirtualBox */

    /* No ACPI listened (real hardware without our own ACPI driver). */
    goodbye_screen("it is now safe to turn off your RedPanda");
    __asm__ __volatile__("cli");
    for (;;)
        __asm__ __volatile__("hlt");
}

/* --- the start menu ----------------------------------------------------------- */

enum {
    MI_NONE = -1,
    MI_WELCOME, MI_EDITOR, MI_SYSTEM, MI_TRASH,
    MI_ABOUT, MI_RESTART, MI_SHUTDOWN,
};

#define MENU_ROWS 7

static const struct {
    const char *label;
    int id;
    int yoff;
} menu_rows[MENU_ROWS] = {
    { "Welcome",        MI_WELCOME,   94 },
    { "Text Editor",    MI_EDITOR,   126 },
    { "System Monitor", MI_SYSTEM,   158 },
    { "Trash",          MI_TRASH,    190 },
    { "About",          MI_ABOUT,    236 },
    { "Restart",        MI_RESTART,  268 },
    { "Shut Down",      MI_SHUTDOWN, 300 },
};

static uint32_t menu_bg_at(int ty)
{
    return lerp_rgb(0x30303F, 0x191922, ty, MENU_H - 1);
}

static int menu_item_at(int mx, int my)
{
    for (int i = 0; i < MENU_ROWS; i++) {
        int ry = menu_y + menu_rows[i].yoff;
        if (mx >= MENU_X + 8 && mx < MENU_X + MENU_W - 8 &&
            my >= ry && my < ry + ROW_H)
            return menu_rows[i].id;
    }
    return MI_NONE;
}

/* 16x16 glyphs, drawn with primitives. `bg` punches holes (ring centers). */

static void glyph_paw(int gx, int gy, uint32_t c)
{
    fb_fill_circle(gx + 8, gy + 10, 5, c);
    fb_fill_circle(gx + 3, gy + 5, 2, c);
    fb_fill_circle(gx + 8, gy + 3, 2, c);
    fb_fill_circle(gx + 13, gy + 5, 2, c);
}

static void glyph_pencil(int gx, int gy, uint32_t c)
{
    fb_line(gx + 4, gy + 11, gx + 11, gy + 4, c);
    fb_line(gx + 5, gy + 12, gx + 12, gy + 5, c);
    fb_line(gx + 6, gy + 13, gx + 13, gy + 6, c);
    fb_fill_rect(gx + 12, gy + 3, 3, 3, c);     /* eraser */
    fb_fill_rect(gx + 3, gy + 12, 2, 2, c);     /* tip */
}

static void glyph_gauge(int gx, int gy, uint32_t c)
{
    fb_fill_rect(gx + 2,  gy + 10, 3, 5,  c);
    fb_fill_rect(gx + 7,  gy + 6,  3, 9,  c);
    fb_fill_rect(gx + 12, gy + 2,  3, 13, c);
}

static void glyph_trash(int gx, int gy, uint32_t c, uint32_t bg)
{
    fb_fill_rect(gx + 6, gy + 1, 4, 2, c);
    fb_fill_rect(gx + 2, gy + 3, 12, 2, c);
    fb_fill_rect(gx + 3, gy + 7, 10, 8, c);
    fb_fill_rect(gx + 6, gy + 9, 1, 4, bg);
    fb_fill_rect(gx + 9, gy + 9, 1, 4, bg);
}

static void glyph_info(int gx, int gy, uint32_t c, uint32_t bg)
{
    fb_fill_circle(gx + 8, gy + 8, 7, c);
    fb_fill_circle(gx + 8, gy + 8, 5, bg);
    fb_fill_rect(gx + 7, gy + 4, 2, 2, c);
    fb_fill_rect(gx + 7, gy + 7, 2, 6, c);
}

static void glyph_restart(int gx, int gy, uint32_t c, uint32_t bg)
{
    fb_fill_circle(gx + 8, gy + 8, 7, c);
    fb_fill_circle(gx + 8, gy + 8, 4, bg);
    fb_fill_rect(gx + 8, gy, 8, 8, bg);         /* open the top-right quadrant */
    fb_fill_rect(gx + 11, gy + 2, 5, 2, c);     /* arrowhead */
    fb_fill_rect(gx + 12, gy + 4, 3, 2, c);
    fb_fill_rect(gx + 13, gy + 6, 1, 1, c);
}

static void glyph_power(int gx, int gy, uint32_t c, uint32_t bg)
{
    fb_fill_circle(gx + 8, gy + 9, 6, c);
    fb_fill_circle(gx + 8, gy + 9, 4, bg);
    fb_fill_rect(gx + 6, gy + 2, 4, 4, bg);     /* the gap in the ring */
    fb_fill_rect(gx + 7, gy + 1, 2, 7, c);      /* the stem */
}

static void draw_menu(void)
{
    int mx = MENU_X, my = menu_y + menu_slide;

    window_shadow(mx, my, MENU_W, MENU_H);

    /* panel: vertical gradient with rounded top and bottom */
    for (int ty = 0; ty < MENU_H; ty++) {
        int inset = 0;
        if (ty < MENU_RADIUS)
            inset = fb_round_inset(ty, MENU_RADIUS);
        else if (ty >= MENU_H - MENU_RADIUS)
            inset = fb_round_inset(MENU_H - 1 - ty, MENU_RADIUS);
        fb_fill_rect(mx + inset, my + ty, MENU_W - 2 * inset, 1, menu_bg_at(ty));
    }
    fb_blend_rect(mx + MENU_RADIUS, my, MENU_W - 2 * MENU_RADIUS, 1, 0xFFFFFF, 50);

    /* header: the resident panda + name */
    draw_panda(mx + 20, my + 14, 2);
    text_shadowed(mx + 62, my + 16, "RedPandaOS", 0xEAEAF2);
    fb_text(mx + 62, my + 36, "version 11", COL_TEXT_DIM);
    fb_blend_rect(mx + 14, my + 64, MENU_W - 28, 1, 0xFFFFFF, 25);

    fb_text(mx + 22, my + 70, "APPS", 0x60607A);

    int hover = menu_slide ? MI_NONE : menu_item_at(mouse_x, mouse_y);

    for (int i = 0; i < MENU_ROWS; i++) {
        int ry = my + menu_rows[i].yoff;
        int hot = (menu_rows[i].id == hover);
        if (hot) {
            fb_blend_rect(mx + 8, ry, MENU_W - 16, ROW_H, 0xFFFFFF, 22);
            fb_fill_rect(mx + 8, ry + 6, 3, ROW_H - 12, COL_ACCENT);
        }
        uint32_t gc = hot ? COL_ACCENT : 0x8E8EA8;
        uint32_t bg = menu_bg_at(menu_rows[i].yoff + 16);
        int gx = mx + 24, gy = ry + 8;
        switch (menu_rows[i].id) {
        case MI_WELCOME:  glyph_paw(gx, gy, gc);          break;
        case MI_EDITOR:   glyph_pencil(gx, gy, gc);       break;
        case MI_SYSTEM:   glyph_gauge(gx, gy, gc);        break;
        case MI_TRASH:    glyph_trash(gx, gy, gc, bg);    break;
        case MI_ABOUT:    glyph_info(gx, gy, gc, bg);     break;
        case MI_RESTART:  glyph_restart(gx, gy, gc, bg);  break;
        case MI_SHUTDOWN: glyph_power(gx, gy, gc, bg);    break;
        }
        fb_text(mx + 52, ry + 8, menu_rows[i].label, hot ? 0xF2F2FA : COL_TEXT);
    }

    fb_blend_rect(mx + 14, my + 228, MENU_W - 28, 1, 0xFFFFFF, 25);
}

/* --- taskbar ----------------------------------------------------------------- */

static void taskbar_button_rect(int slot, int *bx, int *by, int *bw, int *bh)
{
    *bx = 48 + slot * 148;
    *by = scr_h - TASKBAR_H + 5;
    *bw = 140;
    *bh = 26;
}

static void draw_taskbar(void)
{
    int top = scr_h - TASKBAR_H;

    fb_blend_rect(0, top, scr_w, TASKBAR_H, 0x06060A, 150);  /* frosted bar */
    fb_blend_rect(0, top, scr_w, 1, 0xFFFFFF, 40);           /* top hairline */

    /* the start orb: a red panda paw (haloed while the menu is open) */
    int cx = 22, cy = top + TASKBAR_H / 2;
    if (menu_open)
        fb_fill_circle(cx, cy, 15, 0x6B3A20);
    fb_fill_circle(cx, cy, 12, COL_ACCENT);
    fb_fill_circle(cx - 3, cy - 4, 4, 0xD96E33);             /* soft highlight */
    fb_fill_circle(cx, cy + 3, 4, 0xF2E0C8);                 /* main pad */
    fb_fill_circle(cx - 6, cy - 2, 2, 0xF2E0C8);             /* toes */
    fb_fill_circle(cx,     cy - 5, 2, 0xF2E0C8);
    fb_fill_circle(cx + 6, cy - 2, 2, 0xF2E0C8);

    /* one button per open window */
    int slot = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!wins[i].used)
            continue;
        int bx, by, bw, bh;
        taskbar_button_rect(slot, &bx, &by, &bw, &bh);

        int focused_btn = (i == focused && wins[i].visible);
        fb_blend_rect(bx, by, bw, bh, 0xFFFFFF, focused_btn ? 60 : 25);
        if (focused_btn)
            fb_fill_rect(bx + 8, by + bh - 3, bw - 16, 2, COL_ACCENT);

        char t[18];
        int n = 0;
        while (wins[i].title[n] && n < 15) {
            t[n] = wins[i].title[n];
            n++;
        }
        t[n] = 0;
        text_shadowed(bx + 10, by + (bh - FONT_H) / 2, t,
                      wins[i].visible ? 0xD8D8E2 : 0x8888A0);
        slot++;
    }

    /* live clock on the right */
    int hh, mm, ss;
    rtc_time(&hh, &mm, &ss);
    char clock[10], *p = clock;
    p = app_2digit(p, hh); *p++ = ':';
    p = app_2digit(p, mm); *p++ = ':';
    p = app_2digit(p, ss); *p = 0;
    text_shadowed(scr_w - 8 * FONT_W - 14, top + (TASKBAR_H - FONT_H) / 2,
                  clock, 0xE0E0EA);
}

/* --- compositor ----------------------------------------------------------------- */

static void composite(void)
{
    fb_restore(bg_layer);
    draw_desktop_icons();
    if (band_on)
        draw_band();
    for (int zi = 0; zi < zcount; zi++) {
        window_t *w = &wins[zorder[zi]];
        if (w->used && w->visible)
            draw_window(w, zorder[zi] == focused);
    }
    draw_taskbar();
    if (menu_open)
        draw_menu();
    if (icon_dragging)
        draw_drag_ghost();
    fb_flip();
}

/* A quick slide-up so the menu feels like it pops out of the orb. */
static void animate_menu_open(void)
{
    for (int f = 1; f <= 5; f++) {
        menu_slide = (5 - f) * 12;
        composite();
        wait_ticks(2);
    }
    menu_slide = 0;
}

/* --- event handling -------------------------------------------------------------- */

/* Two presses on the same target within half a second = a double click. */
static int is_double(int target)
{
    uint32_t now = timer_ticks();
    int dbl = (target == click_target && now - click_tick < DBLCLICK_TICKS);
    click_target = target;
    click_tick = now;
    return dbl;
}

static int menu_click(int mx, int my)
{
    if (mx < MENU_X || mx >= MENU_X + MENU_W ||
        my < menu_y || my >= menu_y + MENU_H) {
        menu_open = 0;          /* clicking outside closes - and is swallowed */
        return 1;
    }

    int id = menu_item_at(mx, my);
    if (id == MI_NONE)
        return 1;               /* header / separators: stay open */

    menu_open = 0;
    switch (id) {
    case MI_WELCOME:  open_app("Welcome", welcome_draw, 110, 100, 420, 300); break;
    case MI_EDITOR:   open_new_file(); break;
    case MI_SYSTEM:   open_app("System", sysinfo_draw, 600, 240, 330, 250);  break;
    case MI_TRASH:    open_trash_window(); break;
    case MI_ABOUT:    open_app("About", about_draw,
                               (scr_w - 340) / 2, (scr_h - 340) / 2, 340, 310); break;
    case MI_RESTART:  system_restart();  break;
    case MI_SHUTDOWN: system_shutdown(); break;
    }
    return 1;
}

static int handle_button_down(int mx, int my)
{
    /* the start menu swallows everything while it is open */
    if (menu_open)
        return menu_click(mx, my);

    /* taskbar */
    if (my >= scr_h - TASKBAR_H) {
        click_target = -99;

        int dxc = mx - 22, dyc = my - (scr_h - TASKBAR_H / 2);
        if (dxc * dxc + dyc * dyc <= 196) {     /* the paw orb */
            menu_open = 1;
            animate_menu_open();
            return 1;
        }

        int slot = 0;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (!wins[i].used)
                continue;
            int bx, by, bw, bh;
            taskbar_button_rect(slot, &bx, &by, &bw, &bh);
            if (mx >= bx && mx < bx + bw && my >= by && my < by + bh) {
                wins[i].visible = 1;
                raise_window(i);
                return 1;
            }
            slot++;
        }
        return 0;
    }

    /* windows, topmost first */
    for (int zi = zcount - 1; zi >= 0; zi--) {
        int idx = zorder[zi];
        window_t *w = &wins[idx];
        if (!w->visible)
            continue;
        if (mx < w->x || mx >= w->x + w->w || my < w->y || my >= w->y + w->h)
            continue;

        click_target = -99;
        if (my < w->y + TITLE_H) {
            int cy = w->y + TITLE_H / 2;
            int dxc = mx - (w->x + 20), dyc = my - cy;
            if (dxc * dxc + dyc * dyc <= 49) {          /* close light */
                close_window(idx);
                return 1;
            }
            dxc = mx - (w->x + 40);
            if (dxc * dxc + dyc * dyc <= 49) {          /* minimize light */
                w->visible = 0;
                if (focused == idx)
                    focus_topmost_visible();
                return 1;
            }
            raise_window(idx);
            dragging = idx;                             /* grab the title bar */
            drag_dx = mx - w->x;
            drag_dy = my - w->y;
            return 1;
        }

        raise_window(idx);                              /* click body = focus */
        if (w->click)
            w->click(w, mx, my);
        return 1;
    }

    /* the trash: double-click opens it, a drag moves it */
    if (over_trash(mx, my)) {
        clear_selection();
        if (is_double(-2)) {
            trash_armed = 0;
            open_trash_window();
            return 1;
        }
        trash_armed = 1;
        trash_grab_dx = mx - trash_x;
        trash_grab_dy = my - trash_y;
        press_x = mx;
        press_y = my;
        return 1;
    }

    /* file icons */
    int i = icon_at(mx, my);
    if (i >= 0) {
        if (is_double(i)) {
            icon_armed = -1;
            open_editor(icons[i].name);
            return 1;
        }
        if (!icons[i].selected) {       /* fresh click: select just this one */
            clear_selection();
            icons[i].selected = 1;
        }
        icon_armed = i;                 /* may become a drag */
        press_x = mx;
        press_y = my;
        return 1;
    }

    /* empty desktop: start the rubber band */
    click_target = -99;
    clear_selection();
    band_on = 1;
    band_ax = band_x = mx;
    band_ay = band_y = my;
    return 1;
}

static int handle_button_up(int mx, int my)
{
    int dirty = 0;

    dragging = -1;
    if (band_on) {
        band_on = 0;
        dirty = 1;
    }
    if (icon_dragging) {
        if (over_trash(mx, my)) {       /* drop on the trash = delete */
            for (int i = 0; i < icon_count; i++)
                if (icons[i].selected)
                    trash_file(icons[i].name);
            refresh_icons();
        } else {                        /* drop anywhere else = move */
            int dx = mx - press_x, dy = my - press_y;
            for (int i = 0; i < icon_count; i++) {
                if (!icons[i].selected)
                    continue;
                icons[i].x += dx;
                icons[i].y += dy;
                clamp_cell(&icons[i].x, &icons[i].y);
            }
            save_layout();
        }
        icon_dragging = 0;
        dirty = 1;
    }
    if (trash_dragging) {
        trash_dragging = 0;
        save_layout();
        dirty = 1;
    }
    icon_armed = -1;
    trash_armed = 0;
    return dirty;
}

static int handle_event(const event_t *ev)
{
    switch (ev->type) {
    case EV_KEY: {
        uint8_t ch = (uint8_t)ev->ch;
        if (ch == KEY_DEV_CONSOLE) {    /* F12: developers only */
            running = 0;
            return 0;
        }
        if (menu_open) {
            if (ch == 27)
                menu_open = 0;
            return 1;
        }
        /* the focused window gets the keyboard */
        if (focused >= 0 && wins[focused].used && wins[focused].visible &&
            wins[focused].key) {
            wins[focused].key(&wins[focused], ch);
            return 1;
        }
        if (ch == KEY_DEL) {            /* Delete: selected icons -> trash */
            int any = 0;
            for (int i = 0; i < icon_count; i++) {
                if (icons[i].selected) {
                    trash_file(icons[i].name);
                    any = 1;
                }
            }
            if (any) {
                refresh_icons();
                return 1;
            }
        }
        return 0;
    }

    case EV_TICK:
        refresh_icons();                /* the console may have changed files */
        return 1;                       /* clock, live stats */

    case EV_MOUSE_BUTTON:
        if (ev->button == 0 && ev->pressed)
            return handle_button_down(ev->x, ev->y);
        if (ev->button == 0 && !ev->pressed)
            return handle_button_up(ev->x, ev->y);
        return 0;

    case EV_MOUSE_MOVE:
        mouse_x = ev->x;
        mouse_y = ev->y;

        if (menu_open)
            return 1;                   /* live hover highlight */

        if (dragging >= 0) {
            window_t *w = &wins[dragging];
            w->x = ev->x - drag_dx;
            w->y = ev->y - drag_dy;
            /* keep the title bar reachable */
            if (w->x < -w->w + 60)       w->x = -w->w + 60;
            if (w->x > scr_w - 60)       w->x = scr_w - 60;
            if (w->y < 0)                w->y = 0;
            if (w->y > scr_h - TASKBAR_H - 20)
                w->y = scr_h - TASKBAR_H - 20;
            return 1;
        }
        if (band_on) {
            band_x = ev->x;
            band_y = ev->y;
            update_band_selection();
            return 1;
        }
        if (icon_armed >= 0 && !icon_dragging) {
            int dx = ev->x - press_x, dy = ev->y - press_y;
            if (dx * dx + dy * dy > 9)  /* moved a few px: it's a drag */
                icon_dragging = 1;
        }
        if (icon_dragging)
            return 1;
        if (trash_armed && !trash_dragging) {
            int dx = ev->x - press_x, dy = ev->y - press_y;
            if (dx * dx + dy * dy > 9)
                trash_dragging = 1;
        }
        if (trash_dragging) {           /* the trash moves live */
            trash_x = ev->x - trash_grab_dx;
            trash_y = ev->y - trash_grab_dy;
            clamp_cell(&trash_x, &trash_y);
            return 1;
        }
        return 0;
    }
    return 0;
}

/* --- entry point -------------------------------------------------------------------- */

void gui_demo_windows(void)
{
    add_window(110, 100, 420, 300, "Welcome", welcome_draw);
    add_window(600, 240, 330, 250, "System", sysinfo_draw);
}

void gui_run(void)
{
    scr_w = (int)fb_width();
    scr_h = (int)fb_height();
    trash_x = scr_w - ICON_CELL_W - ICON_MARGIN;    /* default; layout overrides */
    trash_y = scr_h - TASKBAR_H - ICON_CELL_H - 12;
    menu_y = scr_h - TASKBAR_H - MENU_H - 10;

    if (!bg_layer) {
        bg_layer = kmalloc(fb_buffer_size());
        render_wallpaper();
        fb_snapshot(bg_layer);
    }

    band_on = 0;
    icon_dragging = 0;
    icon_armed = -1;
    trash_armed = 0;
    trash_dragging = 0;
    menu_open = 0;
    refresh_icons();

    input_flush();
    composite();

    running = 1;
    while (running) {
        event_t ev;
        input_wait(&ev);

        int dirty = 0;
        do {
            dirty |= handle_event(&ev);
        } while (running && input_poll(&ev));

        if (dirty && running)
            composite();
    }

    dragging = -1;
}
