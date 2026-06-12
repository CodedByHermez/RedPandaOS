#include "memmap.h"

/* Must match E820_BUF in entry.asm. */
#define E820_BUF 0x6000

uint32_t memmap_count(void)
{
    return *(volatile uint32_t *)E820_BUF;
}

const e820_entry_t *memmap_entries(void)
{
    return (const e820_entry_t *)(E820_BUF + 4);
}

const char *memmap_type_name(uint32_t type)
{
    switch (type) {
    case E820_USABLE:       return "usable";
    case E820_RESERVED:     return "reserved";
    case E820_ACPI_RECLAIM: return "ACPI reclaimable";
    case E820_ACPI_NVS:     return "ACPI NVS";
    case E820_BAD:          return "bad RAM";
    default:                return "unknown";
    }
}
