/* =============================================================================
 * RedPandaOS - 8259 Programmable Interrupt Controller
 * ============================================================================= */

#ifndef PIC_H
#define PIC_H

#include <stdint.h>

/* Remap hardware IRQs 0-15 to interrupt vectors 32-47 and unmask the ones
 * we handle. (By default they collide with CPU exception vectors 8-15.) */
void pic_init(void);

/* Tell the PIC we finished handling an IRQ so it sends the next one. */
void pic_send_eoi(uint8_t irq);

#endif
