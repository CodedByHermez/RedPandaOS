#include "ata.h"

#include "io.h"

/* Primary IDE channel, master drive - which is exactly what QEMU attaches
 * our raw disk image to. Classic PIO polling, no interrupts needed (IRQ14
 * is masked anyway; reading the status register clears it). */
#define ATA_DATA       0x1F0
#define ATA_ERROR      0x1F1
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA_LO     0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HI     0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7    /* read */
#define ATA_COMMAND    0x1F7    /* write */
#define ATA_ALT_STATUS 0x3F6

#define ST_ERR 0x01
#define ST_DRQ 0x08
#define ST_BSY 0x80

#define CMD_READ     0x20
#define CMD_WRITE    0x30
#define CMD_FLUSH    0xE7
#define CMD_IDENTIFY 0xEC

static uint32_t total_sectors;

/* ~400ns settle delay after selecting a drive: 4 alt-status reads. */
static void io_delay(void)
{
    for (int i = 0; i < 4; i++)
        (void)inb(ATA_ALT_STATUS);
}

static int wait_not_busy(void)
{
    for (int i = 0; i < 1000000; i++)
        if (!(inb(ATA_STATUS) & ST_BSY))
            return 0;
    return -1;
}

static int wait_drq(void)
{
    for (int i = 0; i < 1000000; i++) {
        uint8_t s = inb(ATA_STATUS);
        if (s & ST_ERR)
            return -1;
        if (!(s & ST_BSY) && (s & ST_DRQ))
            return 0;
    }
    return -1;
}

/* Select the drive and program the LBA + sector count registers. */
static int setup(uint32_t lba)
{
    if (wait_not_busy())
        return -1;
    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));   /* master, LBA mode */
    io_delay();
    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LO,  (uint8_t)lba);
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HI,  (uint8_t)(lba >> 16));
    return 0;
}

int ata_init(void)
{
    if (wait_not_busy())
        return -1;
    outb(ATA_DRIVE, 0xE0);
    io_delay();
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);
    outb(ATA_COMMAND, CMD_IDENTIFY);

    if (inb(ATA_STATUS) == 0)
        return -1;              /* no drive on the bus */
    if (wait_drq())
        return -1;

    uint16_t id[256];
    insw(ATA_DATA, id, 256);

    total_sectors = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    return total_sectors ? 0 : -1;
}

uint32_t ata_sectors(void)
{
    return total_sectors;
}

int ata_read(uint32_t lba, void *buf)
{
    if (setup(lba))
        return -1;
    outb(ATA_COMMAND, CMD_READ);
    if (wait_drq())
        return -1;
    insw(ATA_DATA, buf, 256);
    return 0;
}

int ata_write(uint32_t lba, const void *buf)
{
    if (setup(lba))
        return -1;
    outb(ATA_COMMAND, CMD_WRITE);
    if (wait_drq())
        return -1;
    outsw(ATA_DATA, buf, 256);

    /* Flush the drive's cache so the data is really on disk even if the
     * machine is powered off right after. */
    outb(ATA_COMMAND, CMD_FLUSH);
    return wait_not_busy();
}
