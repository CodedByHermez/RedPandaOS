#include "input.h"

/* Ring buffer; size must be a power of two. Producers run in interrupt
 * context, the consumer in the main thread, so reads briefly mask
 * interrupts to stay consistent. */
#define QUEUE_SIZE 128
static event_t queue[QUEUE_SIZE];
static volatile unsigned head, tail;

void input_push(const event_t *ev)
{
    unsigned next = (head + 1) & (QUEUE_SIZE - 1);
    if (next == tail)
        return;                 /* full: drop the newest event */
    queue[head] = *ev;
    head = next;
}

int input_poll(event_t *out)
{
    int got = 0;
    __asm__ __volatile__("cli");
    if (head != tail) {
        *out = queue[tail];
        tail = (tail + 1) & (QUEUE_SIZE - 1);
        got = 1;
    }
    __asm__ __volatile__("sti");
    return got;
}

void input_wait(event_t *out)
{
    for (;;) {
        if (input_poll(out))
            return;
        __asm__ __volatile__("hlt");
    }
}

void input_flush(void)
{
    __asm__ __volatile__("cli");
    tail = head;
    __asm__ __volatile__("sti");
}
