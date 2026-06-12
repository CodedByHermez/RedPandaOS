/* =============================================================================
 * RedPandaOS - paging (virtual memory)
 * ============================================================================= */

#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

/* Identity-map all usable RAM (virtual address == physical address) and
 * switch paging on. Identity mapping keeps every PMM frame and page table
 * directly accessible; non-identity regions (like the heap) are mapped on
 * top with paging_map(). */
void paging_init(void);

/* Map one 4 KB page: virtual address -> physical frame (rw, supervisor). */
void paging_map(uint32_t virt, uint32_t phys);

#endif
