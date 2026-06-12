/* =============================================================================
 * RedPandaOS - BIOS E820 memory map
 * =============================================================================
 * The entry stub collected the map (in real mode, where the BIOS still
 * exists) at a fixed low-memory address. This module is the C-side reader.
 * ============================================================================= */

#ifndef MEMMAP_H
#define MEMMAP_H

#include <stdint.h>

typedef struct {
    uint64_t base;       /* start of the region */
    uint64_t len;        /* length in bytes */
    uint32_t type;       /* E820_USABLE etc. */
    uint32_t attr;       /* ACPI 3.0 extended attributes */
} __attribute__((packed)) e820_entry_t;

#define E820_USABLE       1
#define E820_RESERVED     2
#define E820_ACPI_RECLAIM 3
#define E820_ACPI_NVS     4
#define E820_BAD          5

uint32_t            memmap_count(void);
const e820_entry_t *memmap_entries(void);
const char         *memmap_type_name(uint32_t type);

#endif
