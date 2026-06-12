#include "pmm.h"

#include "memmap.h"
#include "string.h"

/* One bit per 4 KB frame for the full 32-bit address space:
 * 1M frames = 128 KB of bitmap. It lives in BSS, which costs nothing in
 * the kernel binary (NOLOAD) - only RAM, of which we now have a map. */
#define MAX_FRAMES (1u << 20)
static uint32_t bitmap[MAX_FRAMES / 32];

static uint32_t usable_frames;  /* frames the E820 map let us manage */
static uint32_t free_frames;
static uint32_t search_hint;    /* word index where the last alloc succeeded */

static inline void mark_used(uint32_t frame)
{
    bitmap[frame / 32] |= 1u << (frame % 32);
}

static inline void mark_free(uint32_t frame)
{
    bitmap[frame / 32] &= ~(1u << (frame % 32));
}

static inline int is_used(uint32_t frame)
{
    return (bitmap[frame / 32] >> (frame % 32)) & 1;
}

void pmm_init(void)
{
    /* Everything starts reserved; the E820 map opens up the usable parts. */
    memset(bitmap, 0xFF, sizeof(bitmap));

    const e820_entry_t *e = memmap_entries();
    uint32_t n = memmap_count();

    for (uint32_t i = 0; i < n; i++) {
        if (e[i].type != E820_USABLE)
            continue;
        if (e[i].base >> 32)            /* region above 4 GB: out of reach */
            continue;

        uint64_t end64 = e[i].base + e[i].len;
        if (end64 > 0xFFFFF000ull)
            end64 = 0xFFFFF000ull;

        /* Only whole frames inside the region count. */
        uint32_t first = ((uint32_t)e[i].base + FRAME_SIZE - 1) / FRAME_SIZE;
        uint32_t last  = (uint32_t)(end64 / FRAME_SIZE);

        for (uint32_t f = first; f < last; f++) {
            if (is_used(f)) {
                mark_free(f);
                usable_frames++;
                free_frames++;
            }
        }
    }

    /* Re-reserve the first megabyte: the kernel, its BSS (this bitmap!),
     * the stack, the E820 buffer, BIOS data and VGA memory all live there. */
    for (uint32_t f = 0; f < 0x100000 / FRAME_SIZE; f++) {
        if (!is_used(f)) {
            mark_used(f);
            free_frames--;
        }
    }
}

uint32_t pmm_alloc_frame(void)
{
    if (!free_frames)
        return 0;

    for (uint32_t w = search_hint; w < MAX_FRAMES / 32; w++) {
        if (bitmap[w] == 0xFFFFFFFFu)
            continue;
        for (uint32_t b = 0; b < 32; b++) {
            if (!(bitmap[w] & (1u << b))) {
                uint32_t frame = w * 32 + b;
                mark_used(frame);
                free_frames--;
                search_hint = w;
                return frame * FRAME_SIZE;
            }
        }
    }

    /* Hint pointed past the last free frame - rescan from the start once. */
    if (search_hint) {
        search_hint = 0;
        return pmm_alloc_frame();
    }
    return 0;
}

void pmm_free_frame(uint32_t addr)
{
    uint32_t frame = addr / FRAME_SIZE;
    if (is_used(frame)) {
        mark_free(frame);
        free_frames++;
        if (frame / 32 < search_hint)
            search_hint = frame / 32;
    }
}

uint32_t pmm_total_kb(void)
{
    return usable_frames * (FRAME_SIZE / 1024);
}

uint32_t pmm_free_kb(void)
{
    return free_frames * (FRAME_SIZE / 1024);
}
