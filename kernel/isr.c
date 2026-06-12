#include "isr.h"

#include "kprintf.h"
#include "pic.h"
#include "console.h"

static const char *exception_names[32] = {
    "Divide Error",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Error",
    "Virtualization Error",
    "Control Protection Error",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
};

static irq_handler_t irq_handlers[16];

void irq_register(int irq, irq_handler_t handler)
{
    irq_handlers[irq] = handler;
}

/* A CPU exception means the kernel itself did something illegal. There is
 * no one to recover for - show everything we know and stop. */
static void panic(registers_t *r)
{
    console_set_color(COLOR_WHITE, COLOR_RED);
    console_clear();

    kprintf("\n\n   KERNEL PANIC  (the red panda fell out of the tree)\n\n");
    kprintf("   exception %u: %s\n", r->int_no, exception_names[r->int_no]);
    kprintf("   error code: %08x\n", r->err_code);
    if (r->int_no == 14) {      /* page fault: CR2 holds the bad address */
        uint32_t cr2;
        __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
        kprintf("   faulting address (cr2): %08x\n", cr2);
    }
    kprintf("\n");
    kprintf("   eip=%08x  cs=%08x  eflags=%08x\n", r->eip, r->cs, r->eflags);
    kprintf("   eax=%08x ebx=%08x ecx=%08x edx=%08x\n", r->eax, r->ebx, r->ecx, r->edx);
    kprintf("   esi=%08x edi=%08x ebp=%08x esp=%08x\n", r->esi, r->edi, r->ebp, r->esp);
    kprintf("   ds=%08x  es=%08x  fs=%08x  gs=%08x\n", r->ds, r->es, r->fs, r->gs);
    kprintf("\n   System halted.");

    for (;;)
        __asm__ __volatile__("cli; hlt");
}

void interrupt_dispatch(registers_t *r)
{
    if (r->int_no < 32) {
        panic(r);
    } else {
        int irq = (int)(r->int_no - 32);
        if (irq_handlers[irq])
            irq_handlers[irq](r);
        pic_send_eoi((uint8_t)irq);
    }
}
