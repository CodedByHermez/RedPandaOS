/* =============================================================================
 * RedPandaOS - Interrupt Descriptor Table
 * ============================================================================= */

#ifndef IDT_H
#define IDT_H

/* Build the IDT (CPU exceptions + hardware IRQ vectors) and load it. */
void idt_init(void);

#endif
