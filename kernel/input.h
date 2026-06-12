/* =============================================================================
 * RedPandaOS - unified input event queue
 * =============================================================================
 * Keyboard and mouse interrupts push events here; whoever owns the screen
 * (shell command, later the GUI event loop) pulls them out in order.
 * ============================================================================= */

#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

typedef enum {
    EV_KEY = 1,             /* ch = ASCII character */
    EV_MOUSE_MOVE,          /* x, y = new cursor position */
    EV_MOUSE_BUTTON,        /* button (0=left 1=right 2=middle), pressed, x, y */
    EV_TICK,                /* once per second, from the PIT - clocks etc. */
} event_type_t;

typedef struct {
    uint8_t type;
    char    ch;
    uint8_t button;
    uint8_t pressed;
    int16_t x, y;
} event_t;

/* Pseudo-character pushed by the F12 key: opens the developer console.
 * Deliberately unprintable and undocumented in the UI. */
#define KEY_DEV_CONSOLE 0x05

/* Special keys, event-queue only (they never enter the shell's char
 * buffer). Compare against (uint8_t)ev->ch. */
#define KEY_LEFT   0x80
#define KEY_RIGHT  0x81
#define KEY_UP     0x82
#define KEY_DOWN   0x83
#define KEY_HOME   0x84
#define KEY_END    0x85
#define KEY_DEL    0x86

/* Ctrl+letter arrives as the classic ASCII control code (Ctrl+S = 0x13).
 * Only a whitelist of combos is emitted - see keyboard.c. */
#define KEY_CTRL(c) ((c) - 'a' + 1)

void input_push(const event_t *ev);     /* called from IRQ handlers */
int  input_poll(event_t *out);          /* non-blocking; 1 = got an event */
void input_wait(event_t *out);          /* sleeps (hlt) until an event */
void input_flush(void);                 /* drop anything queued */

#endif
