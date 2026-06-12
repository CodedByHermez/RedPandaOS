#include "mouse.h"

#include "fb.h"
#include "input.h"
#include "io.h"
#include "isr.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

/* --- PS/2 controller helpers (bounded spins - never hang the boot) ------ */

static void ps2_wait_write(void)
{
    for (int i = 0; i < 100000; i++)
        if (!(inb(PS2_STATUS) & 2))
            return;
}

static void ps2_wait_read(void)
{
    for (int i = 0; i < 100000; i++)
        if (inb(PS2_STATUS) & 1)
            return;
}

/* Send one byte to the mouse (0xD4 = "next byte goes to the aux device")
 * and swallow the 0xFA acknowledge it answers with. */
static void mouse_write(uint8_t b)
{
    ps2_wait_write();
    outb(PS2_CMD, 0xD4);
    ps2_wait_write();
    outb(PS2_DATA, b);
    ps2_wait_read();
    (void)inb(PS2_DATA);        /* ACK */
}

/* --- packet decoding ----------------------------------------------------- */

static uint8_t packet[3];
static int cycle;
static int mx, my;              /* cursor position, hotspot coords */
static uint8_t buttons;         /* bit 0 left, 1 right, 2 middle */

/* Mild acceleration: slow movements stay 1:1 for precision, fast flicks
 * travel further - the "feels like a real OS" curve. */
static int accel(int d)
{
    int ad = d < 0 ? -d : d;
    if (ad > 10)
        return d * 3;
    if (ad > 5)
        return d * 2;
    return d;
}

static void process_packet(void)
{
    uint8_t flags = packet[0];

    if (flags & 0xC0)           /* X/Y overflow: garbage, drop it */
        return;

    int dx = packet[1] - ((flags & 0x10) ? 256 : 0);
    int dy = packet[2] - ((flags & 0x20) ? 256 : 0);

    if (dx || dy) {
        mx += accel(dx);
        my -= accel(dy);        /* PS/2 y grows upward, screens downward */
        if (mx < 0) mx = 0;
        if (my < 0) my = 0;
        if (mx > (int)fb_width() - 1)  mx = (int)fb_width() - 1;
        if (my > (int)fb_height() - 1) my = (int)fb_height() - 1;

        fb_cursor_move(mx, my);

        event_t ev = { .type = EV_MOUSE_MOVE, .x = (int16_t)mx, .y = (int16_t)my };
        input_push(&ev);
    }

    uint8_t now = flags & 7;
    uint8_t changed = now ^ buttons;
    buttons = now;
    for (uint8_t b = 0; b < 3; b++) {
        if (changed & (1 << b)) {
            event_t ev = {
                .type = EV_MOUSE_BUTTON,
                .button = b,
                .pressed = (now >> b) & 1,
                .x = (int16_t)mx,
                .y = (int16_t)my,
            };
            input_push(&ev);
        }
    }
}

static void mouse_irq(registers_t *r)
{
    (void)r;
    uint8_t b = inb(PS2_DATA);

    /* Byte 0 of every packet has bit 3 set - use that to stay in sync. */
    if (cycle == 0 && !(b & 0x08))
        return;

    packet[cycle++] = b;
    if (cycle == 3) {
        cycle = 0;
        process_packet();
    }
}

void mouse_init(void)
{
    mx = (int)fb_width() / 2;
    my = (int)fb_height() / 2;

    /* Enable the auxiliary (mouse) port on the controller. */
    ps2_wait_write();
    outb(PS2_CMD, 0xA8);

    /* Controller config: turn on IRQ12 (bit 1), make sure the mouse clock
     * runs (clear bit 5). Keyboard bits stay untouched. */
    ps2_wait_write();
    outb(PS2_CMD, 0x20);
    ps2_wait_read();
    uint8_t cfg = inb(PS2_DATA);
    cfg |= 0x02;
    cfg &= (uint8_t)~0x20;
    ps2_wait_write();
    outb(PS2_CMD, 0x60);
    ps2_wait_write();
    outb(PS2_DATA, cfg);

    mouse_write(0xF6);          /* restore defaults */
    mouse_write(0xF3);          /* sample rate... */
    mouse_write(200);           /* ...200 packets/s for smooth motion */
    mouse_write(0xF4);          /* enable reporting */

    /* Drain anything left over so the packet state machine starts clean. */
    while (inb(PS2_STATUS) & 1)
        (void)inb(PS2_DATA);

    irq_register(12, mouse_irq);
}
