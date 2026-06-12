/* =============================================================================
 * RedPandaOS - kernel heap
 * =============================================================================
 * Our own malloc. Lives in a dedicated virtual region (it is NOT identity-
 * mapped - the virtual heap addresses bear no relation to the physical
 * frames behind them) and grows on demand: PMM frame -> paging_map -> more
 * heap. First-fit free list with block splitting and coalescing.
 * ============================================================================= */

#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>
#include <stdint.h>

#define HEAP_BASE 0xD0000000u

void  heap_init(void);
void *kmalloc(size_t size);
void  kfree(void *ptr);

uint32_t heap_mapped_bytes(void);   /* virtual space currently backed by RAM */
uint32_t heap_used_bytes(void);     /* sum of allocated payloads */

#endif
