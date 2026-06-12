#include "heap.h"

#include "paging.h"
#include "pmm.h"

#define ALIGN8(n) (((n) + 7u) & ~7u)
#define MIN_EXPAND_PAGES 4      /* grow at least 16 KB at a time */

/* Every block, used or free, starts with this header; the payload follows
 * immediately after it. */
typedef struct block {
    uint32_t size;              /* payload bytes */
    uint32_t free;
    struct block *next;         /* next block by address */
} block_t;

static block_t *head;
static uint32_t mapped;         /* bytes of virtual space backed by frames */

static block_t *block_end(block_t *b)
{
    return (block_t *)((uint8_t *)(b + 1) + b->size);
}

/* Merge every pair of address-adjacent free blocks. */
static void coalesce(void)
{
    for (block_t *b = head; b && b->next; ) {
        if (b->free && b->next->free && block_end(b) == b->next) {
            b->size += sizeof(block_t) + b->next->size;
            b->next = b->next->next;
        } else {
            b = b->next;
        }
    }
}

/* Map fresh frames at the top of the heap and append them as a free block. */
static int expand(uint32_t min_bytes)
{
    uint32_t pages = (min_bytes + sizeof(block_t) + FRAME_SIZE - 1) / FRAME_SIZE;
    if (pages < MIN_EXPAND_PAGES)
        pages = MIN_EXPAND_PAGES;

    uint32_t grown = 0;
    for (uint32_t i = 0; i < pages; i++) {
        uint32_t frame = pmm_alloc_frame();
        if (!frame)
            break;              /* out of physical memory */
        paging_map(HEAP_BASE + mapped + grown, frame);
        grown += FRAME_SIZE;
    }
    if (!grown)
        return 0;

    block_t *b = (block_t *)(HEAP_BASE + mapped);
    mapped += grown;
    b->size = grown - sizeof(block_t);
    b->free = 1;
    b->next = 0;

    if (!head) {
        head = b;
    } else {
        block_t *last = head;
        while (last->next)
            last = last->next;
        last->next = b;
    }
    coalesce();                 /* new block may touch a trailing free block */
    return 1;
}

void heap_init(void)
{
    expand(1);                  /* start with the minimum mapping */
}

void *kmalloc(size_t size)
{
    if (!size)
        return 0;
    uint32_t want = ALIGN8((uint32_t)size);

    for (int attempt = 0; attempt < 2; attempt++) {
        for (block_t *b = head; b; b = b->next) {
            if (!b->free || b->size < want)
                continue;

            /* Split if the remainder still fits a header + smallest payload. */
            if (b->size >= want + sizeof(block_t) + 8) {
                block_t *rest = (block_t *)((uint8_t *)(b + 1) + want);
                rest->size = b->size - want - sizeof(block_t);
                rest->free = 1;
                rest->next = b->next;
                b->size = want;
                b->next = rest;
            }
            b->free = 0;
            return b + 1;
        }
        if (!expand(want))
            return 0;           /* truly out of memory */
    }
    return 0;
}

void kfree(void *ptr)
{
    if (!ptr)
        return;
    block_t *b = (block_t *)ptr - 1;
    b->free = 1;
    coalesce();
}

uint32_t heap_mapped_bytes(void)
{
    return mapped;
}

uint32_t heap_used_bytes(void)
{
    uint32_t used = 0;
    for (block_t *b = head; b; b = b->next)
        if (!b->free)
            used += b->size;
    return used;
}
