#include "paging.h"

#include "memmap.h"
#include "pmm.h"
#include "string.h"
#include "videoinfo.h"

/* Page directory/table entry flags. */
#define PTE_PRESENT 0x1
#define PTE_WRITE   0x2

/* The page directory: 1024 entries, each covering 4 MB of virtual space.
 * Page tables themselves are allocated from the PMM as needed. */
static uint32_t page_dir[1024] __attribute__((aligned(4096)));

void paging_map(uint32_t virt, uint32_t phys)
{
    uint32_t pd_idx = virt >> 22;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;
    uint32_t *pt;

    if (page_dir[pd_idx] & PTE_PRESENT) {
        pt = (uint32_t *)(page_dir[pd_idx] & 0xFFFFF000u);
    } else {
        /* New page table. Its frame is identity-mapped (or paging is still
         * off), so we can write to it at its physical address. */
        pt = (uint32_t *)pmm_alloc_frame();
        memset(pt, 0, FRAME_SIZE);
        page_dir[pd_idx] = (uint32_t)pt | PTE_PRESENT | PTE_WRITE;
    }

    pt[pt_idx] = (phys & 0xFFFFF000u) | PTE_PRESENT | PTE_WRITE;
    __asm__ __volatile__("invlpg (%0)" : : "r"(virt) : "memory");
}

static void identity_map_range(uint32_t start, uint32_t end)
{
    for (uint32_t p = start & ~(FRAME_SIZE - 1); p < end; p += FRAME_SIZE)
        paging_map(p, p);
}

void paging_init(void)
{
    /* First megabyte: kernel, BSS, stack, VGA text memory, BIOS areas. */
    identity_map_range(0, 0x100000);

    /* All usable RAM, so every frame the PMM hands out is reachable. */
    const e820_entry_t *e = memmap_entries();
    uint32_t n = memmap_count();
    for (uint32_t i = 0; i < n; i++) {
        if (e[i].type != E820_USABLE || (e[i].base >> 32))
            continue;
        uint64_t end64 = e[i].base + e[i].len;
        if (end64 > 0xFFFFF000ull)
            end64 = 0xFFFFF000ull;
        identity_map_range((uint32_t)e[i].base, (uint32_t)end64);
    }

    /* The linear framebuffer is device memory (PCI BAR) - it appears in no
     * E820 entry, so it needs its own mapping or the first pixel write
     * after this function would page-fault. */
    const video_info_t *vi = video_info();
    if (vi->lfb)
        identity_map_range(vi->lfb, vi->lfb + vi->pitch * vi->height);

    /* Load CR3 and flip the paging bit in CR0. The page directory and all
     * tables sit in identity-mapped memory, so nothing moves under us. */
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(page_dir));
    uint32_t cr0;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0));
}
