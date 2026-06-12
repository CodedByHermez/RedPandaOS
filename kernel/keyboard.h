/* =============================================================================
 * RedPandaOS - PS/2 keyboard driver
 * ============================================================================= */

#ifndef KEYBOARD_H
#define KEYBOARD_H

/* Hook the keyboard onto IRQ1. */
void keyboard_init(void);

/* Blocking: sleeps (hlt) until a key arrives, then returns its character.
 * Requires interrupts to be enabled. */
char keyboard_getchar(void);

#endif
