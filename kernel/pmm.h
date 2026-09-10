#ifndef PMM_H
#define PMM_H

#include "../include/types.h"

#define PMM_PAGE_SIZE 4096u
#define PMM_MAX_MEMORY 0x08000000u
#define PMM_MAX_FRAMES (PMM_MAX_MEMORY / PMM_PAGE_SIZE)
#define PMM_BITMAP_BYTES ((PMM_MAX_FRAMES + 7u) / 8u)
#define E820_MAP_ADDR 0x8000u
#define E820_MAX_ENTRIES 128u

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi;
} __attribute__((packed)) e820_entry_t;

void pmm_init(void);
uint32_t pmm_alloc_frame(void);
void pmm_free_frame(uint32_t addr);
uint32_t pmm_total_frames(void);
uint32_t pmm_used_frames(void);
uint32_t pmm_free_frames(void);
uint32_t pmm_total_bytes(void);
uint32_t pmm_used_bytes(void);
uint32_t pmm_free_bytes(void);

#endif
