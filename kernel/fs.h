/* =============================================================================
 * RedPandaOS - RPFS, our own filesystem
 * =============================================================================
 * A deliberately simple on-disk format (everything is 512-byte sectors):
 *
 *   [ superblock ][ FAT ][ root directory ][ data blocks ... ]
 *
 *   - superblock: magic, geometry (where everything lives).
 *   - FAT: one 16-bit entry per data block forming chains, like classic
 *     FAT: 0 = free, 0xFFFF = end of chain, else next block number.
 *     Block numbers start at 1 so "0" can mean free.
 *   - root directory: 64 fixed 32-byte entries (flat - no subdirectories
 *     yet). name, size, first block.
 *
 * The filesystem region starts right after the kernel on the boot disk and
 * survives rebuilds and reboots. If the superblock magic is missing, the
 * kernel formats the region on first boot.
 * ============================================================================= */

#ifndef FS_H
#define FS_H

#include <stdint.h>

/* First sector of the filesystem = boot sector + kernel region.
 * MUST equal 1 + $KernelSectors in build.ps1. */
#define FS_START_LBA 193

#define FS_MAX_NAME  21         /* chars, NUL-terminated in a 22-byte field */
#define FS_DIR_ENTRIES 64

/* Mount the filesystem (formats the region on first boot).
 * Returns 0 on success. */
int fs_mount(void);

/* Directory listing: returns 1 and fills out name/size if slot `idx`
 * (0..FS_DIR_ENTRIES-1) holds a file, 0 otherwise. */
int fs_dir_get(int idx, char name[FS_MAX_NAME + 1], uint32_t *size);

/* Whole-file operations. fs_read returns the file size (copies at most
 * maxlen bytes), or -1 if the file doesn't exist. fs_write creates or
 * replaces. Both return -1 on error. */
int fs_read(const char *name, void *buf, uint32_t maxlen);
int fs_write(const char *name, const void *data, uint32_t len);
int fs_delete(const char *name);

/* Rename in place (directory entry only - data blocks don't move).
 * Fails if `newname` already exists. */
int fs_rename(const char *oldname, const char *newname);

uint32_t fs_free_bytes(void);
uint32_t fs_total_bytes(void);
int      fs_file_count(void);

#endif
