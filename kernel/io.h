/* =============================================================================
 * RedPandaOS - x86 port I/O
 * =============================================================================
 * Talking to hardware on x86 happens through I/O ports (in/out instructions).
 * C has no concept of ports, so these are thin inline-assembly wrappers.
 * ============================================================================= */

#ifndef IO_H
#define IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ __volatile__("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t value;
    __asm__ __volatile__("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* Bulk word transfers - the ATA driver moves whole sectors with these. */
static inline void insw(uint16_t port, void *addr, uint32_t count)
{
    __asm__ __volatile__("rep insw"
                         : "+D"(addr), "+c"(count)
                         : "d"(port)
                         : "memory");
}

static inline void outsw(uint16_t port, const void *addr, uint32_t count)
{
    __asm__ __volatile__("rep outsw"
                         : "+S"(addr), "+c"(count)
                         : "d"(port));
}

#endif
