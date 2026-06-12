#include "fs.h"

#include "ata.h"
#include "heap.h"
#include "string.h"

#define FS_MAGIC   0x53465052u  /* "RPFS" little-endian */
#define FS_VERSION 1
#define BLOCK_SIZE 512
#define FAT_FREE   0x0000
#define FAT_END    0xFFFF

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t fs_sectors;        /* total sectors in the FS region */
    uint32_t fat_start;         /* absolute LBA */
    uint32_t fat_sectors;
    uint32_t dir_start;         /* absolute LBA */
    uint32_t dir_sectors;
    uint32_t data_start;        /* absolute LBA of data block 1 */
    uint32_t data_blocks;       /* usable data blocks (numbered 1..n) */
} __attribute__((packed)) superblock_t;

typedef struct {
    char     name[FS_MAX_NAME + 1];
    uint8_t  used;
    uint8_t  flags;
    uint32_t size;
    uint16_t first;             /* first data block, 0 = empty file */
    uint16_t _pad;
} __attribute__((packed)) dirent_t;

static superblock_t sb;
static uint16_t *fat;           /* cached FAT, written through on change */
static dirent_t *dir;           /* cached directory, written through */
static int mounted;

static uint8_t sect[BLOCK_SIZE];

/* --- write-through helpers ------------------------------------------------ */

static int flush_fat_range(uint32_t first_entry, uint32_t last_entry)
{
    uint32_t s0 = first_entry * 2 / BLOCK_SIZE;
    uint32_t s1 = last_entry * 2 / BLOCK_SIZE;
    for (uint32_t s = s0; s <= s1; s++)
        if (ata_write(sb.fat_start + s, (uint8_t *)fat + s * BLOCK_SIZE))
            return -1;
    return 0;
}

static int flush_dir(void)
{
    for (uint32_t s = 0; s < sb.dir_sectors; s++)
        if (ata_write(sb.dir_start + s, (uint8_t *)dir + s * BLOCK_SIZE))
            return -1;
    return 0;
}

/* --- mount / format -------------------------------------------------------- */

static int fs_format(void)
{
    uint32_t fs_sectors = ata_sectors() - FS_START_LBA;

    sb.magic       = FS_MAGIC;
    sb.version     = FS_VERSION;
    sb.fs_sectors  = fs_sectors;
    /* FAT sized for fs_sectors entries: slightly generous, always enough. */
    sb.fat_sectors = (fs_sectors * 2 + BLOCK_SIZE - 1) / BLOCK_SIZE;
    sb.fat_start   = FS_START_LBA + 1;
    sb.dir_sectors = (FS_DIR_ENTRIES * sizeof(dirent_t)) / BLOCK_SIZE;
    sb.dir_start   = sb.fat_start + sb.fat_sectors;
    sb.data_start  = sb.dir_start + sb.dir_sectors;
    sb.data_blocks = fs_sectors - 1 - sb.fat_sectors - sb.dir_sectors;
    if ((int32_t)sb.data_blocks < 1)
        return -1;
    if (sb.data_blocks > 0xFFFE)
        sb.data_blocks = 0xFFFE;        /* 16-bit chain values */

    memset(fat, 0, sb.fat_sectors * BLOCK_SIZE);
    memset(dir, 0, sb.dir_sectors * BLOCK_SIZE);

    if (flush_fat_range(0, sb.fs_sectors - 1))
        return -1;
    if (flush_dir())
        return -1;

    memset(sect, 0, BLOCK_SIZE);
    memcpy(sect, &sb, sizeof(sb));
    return ata_write(FS_START_LBA, sect);
}

int fs_mount(void)
{
    if (ata_init())
        return -1;
    if (ata_sectors() <= FS_START_LBA + 16)
        return -1;              /* disk too small - shouldn't happen */

    if (ata_read(FS_START_LBA, sect))
        return -1;
    memcpy(&sb, sect, sizeof(sb));

    int fresh = (sb.magic != FS_MAGIC || sb.version != FS_VERSION);
    if (fresh) {
        /* Compute geometry first so we know how much cache to allocate. */
        uint32_t fs_sectors = ata_sectors() - FS_START_LBA;
        sb.fat_sectors = (fs_sectors * 2 + BLOCK_SIZE - 1) / BLOCK_SIZE;
        sb.dir_sectors = (FS_DIR_ENTRIES * sizeof(dirent_t)) / BLOCK_SIZE;
    }

    fat = kmalloc(sb.fat_sectors * BLOCK_SIZE);
    dir = kmalloc(sb.dir_sectors * BLOCK_SIZE);
    if (!fat || !dir)
        return -1;

    if (fresh) {
        if (fs_format())
            return -1;
    } else {
        for (uint32_t s = 0; s < sb.fat_sectors; s++)
            if (ata_read(sb.fat_start + s, (uint8_t *)fat + s * BLOCK_SIZE))
                return -1;
        for (uint32_t s = 0; s < sb.dir_sectors; s++)
            if (ata_read(sb.dir_start + s, (uint8_t *)dir + s * BLOCK_SIZE))
                return -1;
    }

    mounted = 1;
    return 0;
}

/* --- internals --------------------------------------------------------------- */

static uint32_t block_lba(uint16_t block)
{
    return sb.data_start + (uint32_t)(block - 1);
}

static dirent_t *find(const char *name)
{
    for (int i = 0; i < FS_DIR_ENTRIES; i++)
        if (dir[i].used && !strcmp(dir[i].name, name))
            return &dir[i];
    return 0;
}

static void free_chain(uint16_t first)
{
    uint16_t b = first;
    while (b && b != FAT_END) {
        uint16_t next = fat[b];
        fat[b] = FAT_FREE;
        b = next;
    }
}

static uint16_t alloc_block(void)
{
    for (uint32_t b = 1; b <= sb.data_blocks; b++)
        if (fat[b] == FAT_FREE)
            return (uint16_t)b;
    return 0;
}

/* --- public API ----------------------------------------------------------------- */

int fs_dir_get(int idx, char name[FS_MAX_NAME + 1], uint32_t *size)
{
    if (!mounted || idx < 0 || idx >= FS_DIR_ENTRIES || !dir[idx].used)
        return 0;
    memcpy(name, dir[idx].name, FS_MAX_NAME + 1);
    name[FS_MAX_NAME] = '\0';
    *size = dir[idx].size;
    return 1;
}

int fs_read(const char *name, void *buf, uint32_t maxlen)
{
    if (!mounted)
        return -1;
    dirent_t *e = find(name);
    if (!e)
        return -1;

    uint32_t remaining = e->size < maxlen ? e->size : maxlen;
    uint8_t *out = buf;
    uint16_t b = e->first;

    while (remaining && b && b != FAT_END) {
        if (ata_read(block_lba(b), sect))
            return -1;
        uint32_t n = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
        memcpy(out, sect, n);
        out += n;
        remaining -= n;
        b = fat[b];
    }
    return (int)e->size;
}

int fs_write(const char *name, const void *data, uint32_t len)
{
    if (!mounted || !name[0] || strlen(name) > FS_MAX_NAME)
        return -1;

    /* Find the existing entry or claim a fresh slot. */
    dirent_t *e = find(name);
    if (!e) {
        for (int i = 0; i < FS_DIR_ENTRIES; i++) {
            if (!dir[i].used) {
                e = &dir[i];
                break;
            }
        }
        if (!e)
            return -1;          /* directory full */
        memset(e, 0, sizeof(*e));
        memcpy(e->name, name, strlen(name) + 1);
        e->used = 1;
    }

    /* Replace contents: drop the old chain, build a new one. */
    free_chain(e->first);
    e->first = 0;
    e->size = len;

    const uint8_t *in = data;
    uint32_t remaining = len;
    uint16_t prev = 0;

    while (remaining) {
        uint16_t b = alloc_block();
        if (!b) {               /* disk full: roll back to an empty file */
            free_chain(e->first);
            e->first = 0;
            e->size = 0;
            flush_fat_range(0, sb.data_blocks);
            flush_dir();
            return -1;
        }
        fat[b] = FAT_END;
        if (prev)
            fat[prev] = b;
        else
            e->first = b;

        uint32_t n = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
        memset(sect, 0, BLOCK_SIZE);
        memcpy(sect, in, n);
        if (ata_write(block_lba(b), sect))
            return -1;
        in += n;
        remaining -= n;
        prev = b;
    }

    if (flush_fat_range(0, sb.data_blocks))
        return -1;
    return flush_dir();
}

int fs_delete(const char *name)
{
    if (!mounted)
        return -1;
    dirent_t *e = find(name);
    if (!e)
        return -1;

    free_chain(e->first);
    memset(e, 0, sizeof(*e));

    if (flush_fat_range(0, sb.data_blocks))
        return -1;
    return flush_dir();
}

int fs_rename(const char *oldname, const char *newname)
{
    if (!mounted || !newname[0] || strlen(newname) > FS_MAX_NAME)
        return -1;
    dirent_t *e = find(oldname);
    if (!e || find(newname))
        return -1;

    memset(e->name, 0, sizeof(e->name));
    memcpy(e->name, newname, strlen(newname) + 1);
    return flush_dir();
}

uint32_t fs_free_bytes(void)
{
    if (!mounted)
        return 0;
    uint32_t free_blocks = 0;
    for (uint32_t b = 1; b <= sb.data_blocks; b++)
        if (fat[b] == FAT_FREE)
            free_blocks++;
    return free_blocks * BLOCK_SIZE;
}

uint32_t fs_total_bytes(void)
{
    return mounted ? sb.data_blocks * BLOCK_SIZE : 0;
}

int fs_file_count(void)
{
    if (!mounted)
        return 0;
    int n = 0;
    for (int i = 0; i < FS_DIR_ENTRIES; i++)
        if (dir[i].used)
            n++;
    return n;
}
