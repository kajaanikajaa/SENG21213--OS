/* Lecture 11: Physical memory manager using one bit per 4 KiB frame. */
#include "pmm.h"

extern uint8_t _kernel_end;

static uint8_t frame_bitmap[PMM_BITMAP_BYTES];
static uint32_t frame_count;
static uint32_t total_frames_count;
static uint32_t used_frames_count;
static uint32_t reserved_end;

static void bit_set(uint32_t frame)
{
    frame_bitmap[frame >> 3] |= (uint8_t)(1u << (frame & 7u));
}

static void bit_clear(uint32_t frame)
{
    frame_bitmap[frame >> 3] &= (uint8_t)~(1u << (frame & 7u));
}

static uint8_t bit_test(uint32_t frame)
{
    return (uint8_t)((frame_bitmap[frame >> 3] >> (frame & 7u)) & 1u);
}

static uint32_t align_up_page(uint32_t value)
{
    return (value + PMM_PAGE_SIZE - 1u) & ~(PMM_PAGE_SIZE - 1u);
}


void pmm_init(void)
{
    volatile uint16_t *count_ptr = (volatile uint16_t *)E820_MAP_ADDR;
    volatile e820_entry_t *entries = (volatile e820_entry_t *)(E820_MAP_ADDR + 4u);
    uint32_t i;
    uint32_t max_end = 0;
    uint32_t usable = 0;
    uint32_t kernel_end = align_up_page((uint32_t)&_kernel_end);

    reserved_end = kernel_end;

    for (i = 0; i < PMM_BITMAP_BYTES; i++) frame_bitmap[i] = 0xFF;
    total_frames_count = 0;
    used_frames_count = 0;

    /* Lecture 11: E820 type 1 entries describe usable physical memory. */
    for (i = 0; i < *count_ptr && i < E820_MAX_ENTRIES; i++) {
        uint64_t base = entries[i].base;
        uint64_t len = entries[i].length;
        uint64_t end = base + len;
        uint32_t start32;
        uint32_t end32;
        uint32_t f;

        if (entries[i].type != 1 || len == 0 || base >= PMM_MAX_MEMORY)
            continue;
        if (end > PMM_MAX_MEMORY) end = PMM_MAX_MEMORY;
        start32 = (uint32_t)base;
        end32 = (uint32_t)end;
        if (end32 <= start32) continue;
        if (end32 > max_end) max_end = end32;

        start32 = align_up_page(start32);
        end32 &= ~(PMM_PAGE_SIZE - 1u);
        for (f = start32 / PMM_PAGE_SIZE; f < end32 / PMM_PAGE_SIZE; f++) {
            if (bit_test(f)) {
                bit_clear(f);
                usable++;
            }
        }
    }

    frame_count = max_end / PMM_PAGE_SIZE;
    total_frames_count = usable;

    /* Reserve low memory and all pages occupied by the kernel. */
    for (i = 0; i < frame_count; i++) {
        if (!bit_test(i)) {
            uint32_t addr = i * PMM_PAGE_SIZE;
            if (addr < kernel_end || addr < 0x00100000u) {
                bit_set(i);
                used_frames_count++;
            }
        }
    }
}

uint32_t pmm_alloc_frame(void)
{
    uint32_t i;
    for (i = 0; i < frame_count; i++) {
        if (!bit_test(i)) {
            bit_set(i);
            used_frames_count++;
            return i * PMM_PAGE_SIZE;
        }
    }
    return 0;
}

void pmm_free_frame(uint32_t addr)
{
    uint32_t frame = addr / PMM_PAGE_SIZE;
    if (frame >= frame_count || addr % PMM_PAGE_SIZE != 0) return;
    if (addr < reserved_end || addr < 0x00100000u) return;
    if (!bit_test(frame)) return;
    bit_clear(frame);
    if (used_frames_count > 0) used_frames_count--;
}

uint32_t pmm_total_frames(void) { return total_frames_count; }
uint32_t pmm_used_frames(void) { return used_frames_count; }
uint32_t pmm_free_frames(void) { return total_frames_count - used_frames_count; }
uint32_t pmm_total_bytes(void) { return total_frames_count * PMM_PAGE_SIZE; }
uint32_t pmm_used_bytes(void) { return used_frames_count * PMM_PAGE_SIZE; }
uint32_t pmm_free_bytes(void) { return pmm_free_frames() * PMM_PAGE_SIZE; }
