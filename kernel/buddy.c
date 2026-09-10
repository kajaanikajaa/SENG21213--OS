/* Lecture 11 §5: Buddy allocator for contiguous physical memory blocks. */
#include "buddy.h"
#include "pmm.h"

#define BUDDY_POOL_ORDER 8
#define BUDDY_POOL_FRAMES (1u << BUDDY_POOL_ORDER)
#define BUDDY_POOL_SIZE (BUDDY_POOL_FRAMES * PMM_PAGE_SIZE)

typedef struct {
    uint32_t addr;
    uint32_t order;
    uint8_t free;
} buddy_block_t;

#define BUDDY_MAX_BLOCKS 1024u

static buddy_block_t blocks[BUDDY_MAX_BLOCKS];
static uint32_t block_count;
static uint32_t pool_base;
static uint8_t initialized;

static uint32_t block_size(uint32_t order)
{
    return PMM_PAGE_SIZE << order;
}

static int find_free_block(uint32_t addr, uint32_t order)
{
    uint32_t i;

    for (i = 0; i < block_count; i++) {
        if (blocks[i].free &&
            blocks[i].addr == addr &&
            blocks[i].order == order)
            return (int)i;
    }

    return -1;
}

static int add_block(uint32_t addr, uint32_t order)
{
    if (block_count >= BUDDY_MAX_BLOCKS)
        return -1;

    blocks[block_count].addr = addr;
    blocks[block_count].order = order;
    blocks[block_count].free = 1;
    block_count++;

    return (int)(block_count - 1);
}

static void mark_used(uint32_t addr, uint32_t order)
{
    int index = find_free_block(addr, order);

    if (index >= 0)
        blocks[index].free = 0;
}

static uint32_t find_contiguous_pool(void)
{
    uint32_t start;
    uint32_t count;
    uint32_t first;
    uint32_t i;

    count = BUDDY_POOL_FRAMES;

    first = pmm_alloc_frame();

    if (first == 0)
        return 0;

    for (i = 1; i < count; i++) {
        uint32_t addr = pmm_alloc_frame();

        if (addr != first + i * PMM_PAGE_SIZE) {
            uint32_t j;

            pmm_free_frame(first);

            for (j = 1; j < i; j++)
                pmm_free_frame(first + j * PMM_PAGE_SIZE);

            if (addr != 0)
                pmm_free_frame(addr);

            return 0;
        }
    }

    start = first;

    return start;
}

void buddy_init(void)
{
    block_count = 0;
    pool_base = 0;
    initialized = 0;

    /*
     * Reserve one contiguous 1 MB pool from the PMM.
     */
    pool_base = find_contiguous_pool();

    if (pool_base == 0)
        return;

    /*
     * The pool itself is managed by the buddy allocator now.
     */
    add_block(pool_base, BUDDY_POOL_ORDER);

    initialized = 1;
}

uint32_t buddy_alloc(uint32_t order)
{
    uint32_t i;

    if (!initialized || order > BUDDY_POOL_ORDER)
        return 0;

    for (i = 0; i < block_count; i++) {
        if (blocks[i].free &&
            blocks[i].order >= order)
            break;
    }

    if (i == block_count)
        return 0;

    while (blocks[i].order > order) {
        uint32_t old_order = blocks[i].order;
        uint32_t half = block_size(old_order - 1);
        uint32_t second_addr = blocks[i].addr + half;

        blocks[i].order = old_order - 1;

        if (add_block(second_addr, old_order - 1) < 0)
            return 0;
    }

    blocks[i].free = 0;

    return blocks[i].addr;
}

void buddy_free(uint32_t addr, uint32_t order)
{
    uint32_t current_addr;
    uint32_t current_order;

    if (!initialized ||
        addr < pool_base ||
        addr >= pool_base + BUDDY_POOL_SIZE ||
        order > BUDDY_POOL_ORDER)
        return;

    current_addr = addr;
    current_order = order;

    while (current_order < BUDDY_POOL_ORDER) {
        uint32_t buddy_addr;
        int index;

        buddy_addr = current_addr ^ block_size(current_order);

        if (buddy_addr < pool_base ||
            buddy_addr >= pool_base + BUDDY_POOL_SIZE)
            break;

        index = find_free_block(buddy_addr, current_order);

        if (index < 0)
            break;

        blocks[index].free = 0;

        if (buddy_addr < current_addr)
            current_addr = buddy_addr;

        current_order++;
    }

    add_block(current_addr, current_order);
}