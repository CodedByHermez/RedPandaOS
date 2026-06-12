/* =============================================================================
 * RedPandaOS - PIT timer (Intel 8253/8254)
 * ============================================================================= */

#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

/* Program the PIT to fire IRQ0 `freq` times per second. */
void timer_init(uint32_t freq);

uint32_t timer_ticks(void);
uint32_t timer_hz(void);

#endif
