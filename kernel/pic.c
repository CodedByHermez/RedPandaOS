#include "pic.h"

#include "io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define PIC_EOI   0x20

void pic_init(void)
{
    /* Full reinitialization (ICW1-ICW4) of both PIC chips. */
    outb(PIC1_CMD, 0x11);       /* ICW1: init, expect ICW4 */
    outb(PIC2_CMD, 0x11);
    outb(PIC1_DATA, 0x20);      /* ICW2: master IRQs -> vectors 0x20-0x27 */
    outb(PIC2_DATA, 0x28);      /* ICW2: slave  IRQs -> vectors 0x28-0x2F */
    outb(PIC1_DATA, 0x04);      /* ICW3: slave hangs off master IRQ2 */
    outb(PIC2_DATA, 0x02);      /* ICW3: slave's cascade identity */
    outb(PIC1_DATA, 0x01);      /* ICW4: 8086 mode */
    outb(PIC2_DATA, 0x01);

    /* Interrupt masks: enable only what we actually handle.
     * Master: IRQ0 (timer), IRQ1 (keyboard), IRQ2 (cascade) -> 11111000.
     * Slave:  IRQ12 (mouse) -> 11101111. */
    outb(PIC1_DATA, 0xF8);
    outb(PIC2_DATA, 0xEF);
}

void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}
