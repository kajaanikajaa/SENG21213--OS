/* Lecture 11 §5: Slab allocator for small kernel allocations. */
#include "slab.h"
#include "buddy.h"
#include "pmm.h"

#define SLAB_PAGE_ORDER 0
#define SLAB_PAGE_SIZE  PMM_PAGE_SIZE

#define CACHE_COUNT 4

static const uint32_t cache_sizes[CACHE_COUNT] = {
    32, 64, 128, 256
};

typedef struct slab_object {
    struct slab_object *next;
} slab_object_t;

typedef struct slab {
    uint32_t addr;
    uint32_t object_size;
    uint32_t free_count;
    slab_object_t *free_list;
    struct slab *next;
} slab_t;

static slab_t slabs[CACHE_COUNT];
static uint8_t slab_memory[CACHE_COUNT][SLAB_PAGE_SIZE]
    __attribute__((aligned(PMM_PAGE_SIZE)));

static uint8_t slab_initialized;

static int cache_index(uint32_t size)
{
    uint32_t i;

    for (i = 0; i < CACHE_COUNT; i++) {
        if (size <= cache_sizes[i])
            return (int)i;
    }

    return -1;
}

static void create_slab(uint32_t index)
{
    uint32_t object_size;
    uint32_t count;
    uint32_t i;
    uint8_t *base;
    slab_object_t *object;

    object_size = cache_sizes[index];
    count = SLAB_PAGE_SIZE / object_size;
    base = slab_memory[index];

    slabs[index].addr = (uint32_t)base;
    slabs[index].object_size = object_size;
    slabs[index].free_count = count;
    slabs[index].free_list = 0;
    slabs[index].next = 0;

    for (i = 0; i < count; i++) {
        object = (slab_object_t *)(base + i * object_size);
        object->next = slabs[index].free_list;
        slabs[index].free_list = object;
    }
}

void slab_init(void)
{
    uint32_t i;

    for (i = 0; i < CACHE_COUNT; i++)
        create_slab(i);

    slab_initialized = 1;
}

void *kmalloc(uint32_t size)
{
    int index;
    slab_object_t *object;

    if (!slab_initialized || size == 0)
        return 0;

    index = cache_index(size);

    if (index < 0)
        return 0;

    if (slabs[index].free_list == 0)
        return 0;

    object = slabs[index].free_list;
    slabs[index].free_list = object->next;

    if (slabs[index].free_count > 0)
        slabs[index].free_count--;

    return object;
}

void kfree(void *ptr)
{
    uint32_t i;
    uint32_t start;
    uint32_t end;
    uint32_t address;
    slab_object_t *object;

    if (!slab_initialized || ptr == 0)
        return;

    address = (uint32_t)ptr;

    for (i = 0; i < CACHE_COUNT; i++) {
        start = slabs[i].addr;
        end = start + SLAB_PAGE_SIZE;

        if (address >= start && address < end) {
            object = (slab_object_t *)ptr;
            object->next = slabs[i].free_list;
            slabs[i].free_list = object;
            slabs[i].free_count++;
            return;
        }
    }
}