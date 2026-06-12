#include "shell.h"

#include "console.h"
#include "fb.h"
#include "fs.h"
#include "gui.h"
#include "heap.h"
#include "input.h"
#include "keyboard.h"
#include "kprintf.h"
#include "memmap.h"
#include "pmm.h"
#include "string.h"
#include "timer.h"

#define LINE_MAX 128

static void cmd_help(void)
{
    kprintf("  gui             back to the desktop\n");
    kprintf("  demo            open the demo windows, then the desktop\n");
    kprintf("  help            this list\n");
    kprintf("  about           about RedPandaOS\n");
    kprintf("  uptime          time since boot\n");
    kprintf("  ls              list files\n");
    kprintf("  cat <file>      print a file\n");
    kprintf("  write <f> <txt> write text to a file\n");
    kprintf("  rm <file>       delete a file\n");
    kprintf("  mem             memory map and allocator stats\n");
    kprintf("  memtest         exercise kmalloc/kfree\n");
    kprintf("  gfx             graphics demo (any key returns)\n");
    kprintf("  paint           draw with the mouse (ESC exits)\n");
    kprintf("  echo <text>     print <text>\n");
    kprintf("  clear           clear the screen\n");
    kprintf("  crash           invalid opcode -> panic screen\n");
    kprintf("  pagefault       touch unmapped memory -> page fault panic\n");
}

static void cmd_mem(void)
{
    kprintf("  BIOS E820 memory map:\n");
    const e820_entry_t *e = memmap_entries();
    for (uint32_t i = 0; i < memmap_count(); i++) {
        kprintf("    %08x%08x  %8u KB  %s\n",
                (uint32_t)(e[i].base >> 32), (uint32_t)e[i].base,
                (uint32_t)(e[i].len >> 10), memmap_type_name(e[i].type));
    }
    kprintf("\n  physical : %u KB free of %u KB usable (4 KB frames)\n",
            pmm_free_kb(), pmm_total_kb());
    kprintf("  heap     : %u KB mapped at %p, %u bytes in use\n",
            heap_mapped_bytes() / 1024, (void *)HEAP_BASE, heap_used_bytes());
}

static void cmd_memtest(void)
{
    kprintf("  alloc a=8 KB, b=100 KB, c=16 B ... ");
    uint8_t *a = kmalloc(8 * 1024);
    uint8_t *b = kmalloc(100 * 1024);   /* forces the heap to grow */
    uint8_t *c = kmalloc(16);
    if (!a || !b || !c) {
        kprintf("FAILED (out of memory?)\n");
        return;
    }
    kprintf("ok (a=%p b=%p c=%p)\n", a, b, c);

    kprintf("  writing patterns, verifying ... ");
    memset(a, 0xAA, 8 * 1024);
    memset(b, 0x5B, 100 * 1024);
    memset(c, 0xC7, 16);
    for (int i = 0; i < 8 * 1024; i++)
        if (a[i] != 0xAA) { kprintf("FAILED at a[%d]\n", i); return; }
    for (int i = 0; i < 100 * 1024; i++)
        if (b[i] != 0x5B) { kprintf("FAILED at b[%d]\n", i); return; }
    kprintf("ok\n");

    kprintf("  free b, alloc d=32 KB (should reuse b's space) ... ");
    kfree(b);
    uint8_t *d = kmalloc(32 * 1024);
    kprintf("ok (d=%p)\n", d);

    kprintf("  verifying a survived ... ");
    for (int i = 0; i < 8 * 1024; i++)
        if (a[i] != 0xAA) { kprintf("FAILED at a[%d]\n", i); return; }
    kprintf("ok\n");

    kfree(a);
    kfree(c);
    kfree(d);
    kprintf("  all freed. heap: %u KB mapped, %u bytes in use\n",
            heap_mapped_bytes() / 1024, heap_used_bytes());
}

/* Blocky red panda, painted with rectangles on a 16x14 grid. */
static void draw_panda(int ox, int oy, int s)
{
    const uint32_t FUR   = 0xC65A1E;   /* rusty red */
    const uint32_t DARK  = 0x6E3208;   /* ear/eye dark brown */
    const uint32_t CREAM = 0xF2E0C8;   /* face patches */
    const uint32_t INK   = 0x1A1A1A;   /* eyes, nose */

    fb_fill_rect(ox + 1*s,  oy + 0*s, 3*s, 3*s, DARK);     /* left ear */
    fb_fill_rect(ox + 12*s, oy + 0*s, 3*s, 3*s, DARK);     /* right ear */
    fb_fill_rect(ox + 2*s,  oy + 1*s, 1*s, 1*s, CREAM);    /* ear tufts */
    fb_fill_rect(ox + 13*s, oy + 1*s, 1*s, 1*s, CREAM);
    fb_fill_rect(ox + 0*s,  oy + 2*s, 16*s, 12*s, FUR);    /* head */
    fb_fill_rect(ox + 2*s,  oy + 5*s, 4*s, 3*s, CREAM);    /* eye patches */
    fb_fill_rect(ox + 10*s, oy + 5*s, 4*s, 3*s, CREAM);
    fb_fill_rect(ox + 3*s,  oy + 6*s, 2*s, 2*s, INK);      /* eyes */
    fb_fill_rect(ox + 11*s, oy + 6*s, 2*s, 2*s, INK);
    fb_fill_rect(ox + 5*s,  oy + 9*s, 6*s, 5*s, CREAM);    /* muzzle */
    fb_fill_rect(ox + 7*s,  oy + 10*s, 2*s, 2*s, INK);     /* nose */
    fb_fill_rect(ox + 0*s,  oy + 9*s, 2*s, 3*s, CREAM);    /* cheek stripes */
    fb_fill_rect(ox + 14*s, oy + 9*s, 2*s, 3*s, CREAM);
}

static void cmd_gfx(void)
{
    int w = (int)fb_width(), h = (int)fb_height();

    fb_fill_rect(0, 0, w, h, 0x101018);

    fb_text(40, 24, "RedPandaOS v5 - drawing its own pixels", 0xFFFFFF);

    /* RGB gradient band */
    for (int x = 0; x < w - 80; x++) {
        uint32_t t = (uint32_t)x * 255 / (uint32_t)(w - 81);
        fb_fill_rect(40 + x, 56, 1, 48, (t << 16) | ((255 - t) << 8) | 96);
    }

    /* nested rectangle outlines */
    for (int i = 0; i < 9; i++) {
        uint32_t c = 0x2060A0 + (uint32_t)i * 0x101008;
        fb_draw_rect(40 + i * 14, 140 + i * 10, 300 - i * 28, 200 - i * 20, c);
    }

    /* starburst of lines */
    int cx = w - 280, cy = 240;
    for (int i = 0; i <= 16; i++) {
        fb_line(cx, cy, cx - 200 + i * 25, cy - 100, 0xE0A030);
        fb_line(cx, cy, cx - 200 + i * 25, cy + 100, 0x30A0E0);
    }

    draw_panda(w / 2 - 64, h - 220, 8);
    fb_text(w / 2 - 100, h - 80, "the resident red panda", 0xC65A1E);

    fb_text(40, h - 32, "press any key to return", 0x808080);
    fb_flip();

    keyboard_getchar();
    console_clear();
}

/* Append an int to a string buffer (for the paint status line - kprintf
 * only talks to the console, this text goes straight to the framebuffer). */
static char *append_int(char *p, int v)
{
    if (v < 0) {
        *p++ = '-';
        v = -v;
    }
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

static void cmd_paint(void)
{
    const uint32_t BG = 0x16161E, BAR = 0x24242E, INK = 0xF0F0F0;
    int w = (int)fb_width(), h = (int)fb_height();

    fb_fill_rect(0, 0, w, h, BG);
    fb_fill_rect(0, 0, w, 24, BAR);
    fb_text(8, 4, "paint  -  hold left button to draw, right click clears, ESC exits", 0x9090A0);
    fb_flip();

    input_flush();
    int last_x = -1, last_y = -1, left = 0;

    for (;;) {
        event_t ev;
        input_wait(&ev);

        if (ev.type == EV_KEY && ev.ch == 27)
            break;

        if (ev.type == EV_MOUSE_BUTTON) {
            if (ev.button == 0) {
                left = ev.pressed;
                last_x = ev.x;
                last_y = ev.y;
            } else if (ev.button == 1 && ev.pressed) {
                fb_fill_rect(0, 24, w, h - 24, BG);
                fb_flip();
            }
        }

        if (ev.type == EV_MOUSE_MOVE) {
            if (left && last_x >= 0 && ev.y > 24) {
                /* 2px-thick stroke from the last point to this one. */
                for (int oy = 0; oy <= 1; oy++)
                    for (int ox = 0; ox <= 1; ox++)
                        fb_line(last_x + ox, last_y + oy, ev.x + ox, ev.y + oy, INK);

                int x0 = last_x < ev.x ? last_x : ev.x;
                int y0 = last_y < ev.y ? last_y : ev.y;
                int x1 = last_x > ev.x ? last_x : ev.x;
                int y1 = last_y > ev.y ? last_y : ev.y;
                fb_flip_rect(x0 - 1, y0 - 1, x1 - x0 + 4, y1 - y0 + 4);
            }
            last_x = ev.x;
            last_y = ev.y;

            /* Live coordinates in the status bar. */
            char status[32];
            char *p = status;
            p = append_int(p, ev.x);
            *p++ = ',';
            p = append_int(p, ev.y);
            if (left) {
                *p++ = ' ';
                *p++ = 'L';
            }
            *p = '\0';
            fb_fill_rect(w - 120, 0, 120, 24, BAR);
            fb_text(w - 112, 4, status, INK);
            fb_flip_rect(w - 120, 0, 120, 24);
        }
    }

    console_clear();
}

static void cmd_ls(void)
{
    char name[FS_MAX_NAME + 1];
    uint32_t size;
    int n = 0;

    for (int i = 0; i < FS_DIR_ENTRIES; i++) {
        if (fs_dir_get(i, name, &size)) {
            kprintf("  %6u  %s\n", size, name);
            n++;
        }
    }
    if (!n)
        kprintf("  (no files)\n");
    kprintf("  %u of %u KB free\n", fs_free_bytes() / 1024, fs_total_bytes() / 1024);
}

static void cmd_cat(const char *name)
{
    int size = fs_read(name, 0, 0);     /* probe the size first */
    if (size < 0) {
        kprintf("  no such file: %s\n", name);
        return;
    }
    if (size == 0) {
        kprintf("  (empty)\n");
        return;
    }

    char *buf = kmalloc((uint32_t)size + 1);
    if (!buf) {
        kprintf("  out of memory\n");
        return;
    }
    fs_read(name, buf, (uint32_t)size);
    buf[size] = '\0';
    kprintf("%s\n", buf);
    kfree(buf);
}

static void cmd_write(char *args)
{
    /* args = "<name> <text...>" */
    char *text = args;
    while (*text && *text != ' ')
        text++;
    if (!*text) {
        kprintf("  usage: write <file> <text>\n");
        return;
    }
    *text++ = '\0';

    if (fs_write(args, text, (uint32_t)strlen(text)) < 0)
        kprintf("  write failed (name too long? disk full?)\n");
    else
        kprintf("  wrote %u bytes to %s\n", (uint32_t)strlen(text), args);
}

static void cmd_rm(const char *name)
{
    if (fs_delete(name) < 0)
        kprintf("  no such file: %s\n", name);
    else
        kprintf("  deleted %s\n", name);
}

static void cmd_about(void)
{
    console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    kprintf("\n    (\\__/)\n");
    kprintf("    (o^.^)    RedPandaOS v11\n");
    kprintf("    z(_(\")(\")  ");
    console_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    kprintf("own bootloader, own kernel, own everything.\n\n");
}

static void cmd_uptime(void)
{
    uint32_t t = timer_ticks();
    uint32_t hz = timer_hz();
    kprintf("  up %u.%02u s  (%u ticks @ %u Hz)\n",
            t / hz, (t % hz) * 100 / hz, t, hz);
}

static void execute(char *line)
{
    if (!line[0])
        return;

    if (!strcmp(line, "gui")) {
        gui_run();
        console_clear();
        kprintf("Back in the console. 'gui' returns to the desktop.\n");
    } else if (!strcmp(line, "demo")) {
        gui_demo_windows();
        gui_run();
        console_clear();
        kprintf("Back in the console. 'gui' returns to the desktop.\n");
    } else if (!strcmp(line, "ls")) {
        cmd_ls();
    } else if (!strncmp(line, "cat ", 4)) {
        cmd_cat(line + 4);
    } else if (!strncmp(line, "write ", 6)) {
        cmd_write(line + 6);
    } else if (!strncmp(line, "rm ", 3)) {
        cmd_rm(line + 3);
    } else if (!strcmp(line, "help")) {
        cmd_help();
    } else if (!strcmp(line, "about")) {
        cmd_about();
    } else if (!strcmp(line, "uptime")) {
        cmd_uptime();
    } else if (!strcmp(line, "clear")) {
        console_clear();
    } else if (!strncmp(line, "echo ", 5)) {
        kprintf("%s\n", line + 5);
    } else if (!strcmp(line, "echo")) {
        kprintf("\n");
    } else if (!strcmp(line, "gfx")) {
        cmd_gfx();
    } else if (!strcmp(line, "paint")) {
        cmd_paint();
    } else if (!strcmp(line, "mem")) {
        cmd_mem();
    } else if (!strcmp(line, "memtest")) {
        cmd_memtest();
    } else if (!strcmp(line, "crash")) {
        kprintf("  executing an invalid opcode...\n");
        __asm__ __volatile__("ud2");
    } else if (!strcmp(line, "pagefault")) {
        kprintf("  writing to unmapped address 0xE0000000...\n");
        *(volatile uint32_t *)0xE0000000 = 42;
    } else {
        kprintf("  unknown command: %s  (try 'help')\n", line);
    }
}

static void read_line(char *buf)
{
    int len = 0;

    for (;;) {
        char c = keyboard_getchar();

        if (c == '\n') {
            console_putc('\n');
            break;
        }
        if (c == '\b') {
            if (len > 0) {
                len--;
                console_putc('\b');  /* the VGA driver erases the cell */
            }
            continue;
        }
        if (c >= ' ' && len < LINE_MAX - 1) {
            buf[len++] = c;
            console_putc(c);
        }
    }
    buf[len] = '\0';
}

void shell_run(void)
{
    char line[LINE_MAX];

    kprintf("Type 'help' for a list of commands.\n\n");

    for (;;) {
        console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
        kprintf("redpanda");
        console_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
        kprintf("> ");

        read_line(line);
        execute(line);
    }
}
