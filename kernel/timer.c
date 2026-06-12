#include "timer.h"

#include "input.h"
#include "io.h"
#include "isr.h"

/* The PIT's input clock runs at 1.193182 MHz; it fires an IRQ0 every
 * `divisor` clocks. */
#define PIT_BASE_HZ 1193182u
#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43

static volatile uint32_t ticks;
static uint32_t hz;

static void timer_irq(registers_t *r)
{
    (void)r;
    ticks++;

    if (hz && ticks % hz == 0) {        /* second boundary */
        event_t ev = { .type = EV_TICK };
        input_push(&ev);
    }
}

void timer_init(uint32_t freq)
{
    hz = freq;
    uint32_t divisor = PIT_BASE_HZ / freq;

    outb(PIT_COMMAND, 0x36);    /* channel 0, lobyte/hibyte, square wave */
    outb(PIT_CHANNEL0, (uint8_t)divisor);
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));

    irq_register(0, timer_irq);
}

uint32_t timer_ticks(void)
{
    return ticks;
}

uint32_t timer_hz(void)
{
    return hz;
}
