/* =============================================================================
 * RedPandaOS - interrupt service routines
 * =============================================================================
 * When an interrupt fires, the stubs in isr.asm save the complete CPU state
 * on the stack and call interrupt_dispatch() with a pointer to it. The
 * registers_t layout below mirrors the exact push order in isr.asm - if one
 * changes, the other must too.
 * ============================================================================= */

#ifndef ISR_H
#define ISR_H

#include <stdint.h>

typedef struct registers {
    uint32_t gs, fs, es, ds;                          /* pushed by stub */
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pushed by pusha */
    uint32_t int_no, err_code;                        /* stub + CPU */
    uint32_t eip, cs, eflags;                         /* pushed by the CPU */
} registers_t;

typedef void (*irq_handler_t)(registers_t *regs);

/* Register a handler for hardware IRQ 0-15 (not interrupt vector!). */
void irq_register(int irq, irq_handler_t handler);

#endif
