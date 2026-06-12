/* =============================================================================
 * RedPandaOS - ATA disk driver (PIO mode, primary master)
 * ============================================================================= */

#ifndef ATA_H
#define ATA_H

#include <stdint.h>

/* Identify the drive. Returns 0 on success. */
int ata_init(void);

/* Total addressable sectors (LBA28) reported by the drive. */
uint32_t ata_sectors(void);

/* Read/write one 512-byte sector. Return 0 on success, -1 on error. */
int ata_read(uint32_t lba, void *buf);
int ata_write(uint32_t lba, const void *buf);

#endif
