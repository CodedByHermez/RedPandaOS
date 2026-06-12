/* =============================================================================
 * RedPandaOS - physical memory manager
 * =============================================================================
 * Hands out physical RAM in 4 KB frames, tracked by a bitmap (1 bit per
 * frame, set = in use). Built from the BIOS E820 map.
 * ============================================================================= */

#ifndef PMM_H
#define PMM_H

#include <stdint.h>

#define FRAME_SIZE 4096u

void pmm_init(void);

/* Returns the physical address of a free 4 KB frame, or 0 if out of memory. */
uint32_t pmm_alloc_frame(void);
void     pmm_free_frame(uint32_t addr);

uint32_t pmm_total_kb(void);    /* usable RAM managed by the allocator */
uint32_t pmm_free_kb(void);

#endif
