#include "keyboard.h"

#include "input.h"
#include "io.h"
#include "isr.h"

#define KBD_DATA   0x60
#define KBD_STATUS 0x64

#define SC_LSHIFT  0x2A
#define SC_RSHIFT  0x36
#define SC_CAPS    0x3A
#define SC_EXTENDED 0xE0
#define SC_RELEASE 0x80         /* bit set on key-release scancodes */

/* Scancode set 1 -> ASCII (US layout). 0 = key we don't translate. */
static const char keymap[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ',
};

static const char keymap_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ',
};

/* Ring buffer between the interrupt handler (producer) and
 * keyboard_getchar() (consumer). Size must be a power of two. */
#define KB_BUF_SIZE 128
static volatile char kb_buf[KB_BUF_SIZE];
static volatile unsigned kb_head, kb_tail;

static int shift_down;
static int ctrl_down;
static int caps_on;
static int extended;            /* last byte was an 0xE0 prefix */

/* Special keys go ONLY to the event queue (the GUI); the shell's char
 * buffer never sees them. */
static void push_special(uint8_t code)
{
    event_t ev = { .type = EV_KEY, .ch = (char)code };
    input_push(&ev);
}

static void keyboard_irq(registers_t *r)
{
    (void)r;
    uint8_t sc = inb(KBD_DATA);

    if (sc == SC_EXTENDED) {    /* arrows, right-ctrl, del, home, end, ... */
        extended = 1;
        return;
    }
    if (extended) {
        extended = 0;
        switch (sc) {
        case 0x1D: ctrl_down = 1; return;   /* right ctrl */
        case 0x9D: ctrl_down = 0; return;
        case 0x48: push_special(KEY_UP);    return;
        case 0x50: push_special(KEY_DOWN);  return;
        case 0x4B: push_special(KEY_LEFT);  return;
        case 0x4D: push_special(KEY_RIGHT); return;
        case 0x47: push_special(KEY_HOME);  return;
        case 0x4F: push_special(KEY_END);   return;
        case 0x53: push_special(KEY_DEL);   return;
        }
        return;                 /* other extended keys: ignored */
    }

    if (sc & SC_RELEASE) {
        uint8_t key = sc & 0x7F;
        if (key == SC_LSHIFT || key == SC_RSHIFT)
            shift_down = 0;
        if (key == 0x1D)
            ctrl_down = 0;
        return;
    }

    if (sc == SC_LSHIFT || sc == SC_RSHIFT) {
        shift_down = 1;
        return;
    }
    if (sc == 0x1D) {           /* left ctrl */
        ctrl_down = 1;
        return;
    }
    if (sc == SC_CAPS) {
        caps_on = !caps_on;
        return;
    }
    if (sc == 0x58) {           /* F12: developer console (event-only) */
        event_t dev = { .type = EV_KEY, .ch = KEY_DEV_CONSOLE };
        input_push(&dev);
        return;
    }

    char c = shift_down ? keymap_shift[sc] : keymap[sc];
    if (!c)
        return;

    /* Ctrl combos: a small whitelist becomes an ASCII control code in the
     * event queue (Ctrl+S = 0x13). Everything else held under Ctrl is
     * swallowed so stray letters don't leak into whoever is focused. */
    if (ctrl_down) {
        char lo = (char)(c | 0x20);
        if (lo == 's' || lo == 'z' || lo == 'y')
            push_special((uint8_t)KEY_CTRL(lo));
        return;
    }

    /* Caps lock flips the case of letters only. */
    if (caps_on && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
        c ^= 0x20;

    unsigned next = (kb_head + 1) & (KB_BUF_SIZE - 1);
    if (next != kb_tail) {      /* drop the key if the buffer is full */
        kb_buf[kb_head] = c;
        kb_head = next;
    }

    /* Mirror into the unified event queue for event-driven consumers. */
    event_t ev = { .type = EV_KEY, .ch = c };
    input_push(&ev);
}

void keyboard_init(void)
{
    /* Drain anything stale in the controller's output buffer. */
    while (inb(KBD_STATUS) & 1)
        (void)inb(KBD_DATA);

    irq_register(1, keyboard_irq);
}

char keyboard_getchar(void)
{
    for (;;) {
        __asm__ __volatile__("cli");
        if (kb_head != kb_tail) {
            char c = kb_buf[kb_tail];
            kb_tail = (kb_tail + 1) & (KB_BUF_SIZE - 1);
            __asm__ __volatile__("sti");
            return c;
        }
        /* Buffer empty: re-enable interrupts and sleep until the next one.
         * The sti takes effect after the hlt, so no key can slip through
         * in between. */
        __asm__ __volatile__("sti; hlt");
    }
}
