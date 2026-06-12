#include "idt.h"

#include <stdint.h>

/* One IDT gate descriptor (the hardware-mandated layout). */
struct idt_entry {
    uint16_t base_low;   /* handler address bits 0-15 */
    uint16_t selector;   /* code segment selector in the GDT */
    uint8_t  zero;
    uint8_t  flags;      /* present | ring | gate type */
    uint16_t base_high;  /* handler address bits 16-31 */
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];

/* Filled in by isr.asm: the addresses of all 48 interrupt stubs. */
extern uint32_t isr_stub_table[48];

static void set_gate(int n, uint32_t handler)
{
    idt[n].base_low  = handler & 0xFFFF;
    idt[n].selector  = 0x08;        /* kernel code segment */
    idt[n].zero      = 0;
    idt[n].flags     = 0x8E;        /* present, ring 0, 32-bit interrupt gate */
    idt[n].base_high = (uint16_t)(handler >> 16);
}

void idt_init(void)
{
    for (int i = 0; i < 48; i++)
        set_gate(i, isr_stub_table[i]);

    /* Vectors 48-255 stay zeroed (not present): firing one causes a GPF,
     * which our exception handler turns into a readable panic. */

    struct idt_ptr ptr = { sizeof(idt) - 1, (uint32_t)idt };
    __asm__ __volatile__("lidt %0" : : "m"(ptr));
}
