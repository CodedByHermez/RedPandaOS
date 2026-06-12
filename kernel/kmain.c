/* =============================================================================
 * RedPandaOS - kernel main
 * =============================================================================
 * entry.asm calls kmain() once the CPU is in 32-bit protected mode with a
 * flat address space, a stack, and a zeroed BSS. The screen is already in
 * a VBE graphics mode (also entry.asm's doing). From here on: pure C.
 * ============================================================================= */

#include <stdarg.h>

#include "console.h"
#include "cursor.h"
#include "fb.h"
#include "fs.h"
#include "gui.h"
#include "heap.h"
#include "idt.h"
#include "keyboard.h"
#include "kprintf.h"
#include "mouse.h"
#include "paging.h"
#include "pic.h"
#include "pmm.h"
#include "shell.h"
#include "timer.h"
#include "videoinfo.h"

static void boot_ok(const char *fmt, ...)
{
    console_set_color(COLOR_DARK_GREY, COLOR_BLACK);
    kprintf("  [");
    console_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    kprintf("ok");
    console_set_color(COLOR_DARK_GREY, COLOR_BLACK);
    kprintf("] ");
    console_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    kprintf("\n");
}

void kmain(void)
{
    /* Until the heap exists the framebuffer runs unbuffered - pixels go
     * straight to the screen, which is exactly right for early boot. */
    fb_init();
    console_init();

    console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    kprintf("\n  RedPandaOS v11\n");
    console_set_color(COLOR_DARK_GREY, COLOR_BLACK);
    kprintf("  ----------------------------------------\n");

    const video_info_t *vi = video_info();
    boot_ok("VBE framebuffer: %ux%u, %u bpp at %p",
            vi->width, vi->height, vi->bpp, (void *)vi->lfb);

    idt_init();
    boot_ok("IDT loaded (32 exceptions + 16 IRQs)");

    pic_init();
    boot_ok("PIC remapped (IRQ 0-15 -> vectors 32-47)");

    pmm_init();
    boot_ok("physical memory: %u MB usable (E820)", pmm_total_kb() / 1024);

    paging_init();
    boot_ok("paging enabled (RAM + framebuffer mapped)");

    heap_init();
    boot_ok("kernel heap at %p", (void *)HEAP_BASE);

    fb_enable_backbuffer();
    boot_ok("double buffering on (%u KB backbuffer)",
            vi->pitch * vi->height / 1024);

    if (fs_mount() == 0)
        boot_ok("RPFS mounted: %d files, %u KB free",
                fs_file_count(), fs_free_bytes() / 1024);
    else
        boot_ok("RPFS unavailable (no disk?)");

    timer_init(100);
    boot_ok("PIT timer at 100 Hz");

    keyboard_init();
    boot_ok("PS/2 keyboard");

    cursor_init();
    mouse_init();
    boot_ok("PS/2 mouse (IRQ12, 200 Hz)");

    __asm__ __volatile__("sti");
    boot_ok("interrupts enabled");

    kprintf("\n  starting the GUI...\n");
    gui_run();                  /* returns only on the dev key (F12) */

    console_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    console_clear();
    kprintf("RedPandaOS developer console. 'gui' returns to the desktop.\n\n");
    shell_run();                /* never returns */
}
