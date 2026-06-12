/* =============================================================================
 * RedPandaOS - PS/2 mouse driver
 * ============================================================================= */

#ifndef MOUSE_H
#define MOUSE_H

/* Bring up the PS/2 auxiliary device on IRQ12 and start reporting.
 * Movement updates the framebuffer cursor and pushes input events. */
void mouse_init(void);

#endif
