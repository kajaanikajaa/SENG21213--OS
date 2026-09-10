#include "ramdisk.h"
#include "vga.h"

#define JOURNAL_MAGIC       0x4A4E4C31u /* "JNL1" */
#define JOURNAL_BLOCK       254u
#define JOURNAL_DATA_BLOCK  255u

typedef struct {
    uint32_t magic;
    uint32_t target_block;
    uint32_t checksum;
    uint32_t committed;
    uint8_t  reserved[RAMDISK_BLOCK_SIZE - 16];
} journal_header_t;

/* The disk is deliberately a fixed BSS object: no malloc is required. */
static uint8_t ramdisk[RAMDISK_BLOCKS * RAMDISK_BLOCK_SIZE];

static uint32_t checksum32(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    uint32_t i;
    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static void copy_bytes(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) dst[i] = src[i];
}


static void journal_clear(void)
{
    journal_header_t *j = (journal_header_t *)&ramdisk[JOURNAL_BLOCK * RAMDISK_BLOCK_SIZE];
    j->magic = 0;
    j->target_block = 0;
    j->checksum = 0;
    j->committed = 0;
}

static void journal_recover(void)
{
    journal_header_t *j = (journal_header_t *)&ramdisk[JOURNAL_BLOCK * RAMDISK_BLOCK_SIZE];
    uint8_t *old_data = &ramdisk[JOURNAL_DATA_BLOCK * RAMDISK_BLOCK_SIZE];
    uint8_t *target;

    if (j->magic != JOURNAL_MAGIC ||
        j->target_block >= RAMDISK_BLOCKS ||
        j->target_block == JOURNAL_BLOCK ||
        j->target_block == JOURNAL_DATA_BLOCK)
        return;

    if (checksum32(old_data, RAMDISK_BLOCK_SIZE) != j->checksum) {
        journal_clear();
        return;
    }

    /* Uncommitted transaction means the target may contain a partial update.
     * Restore its before-image. A committed transaction is already durable. */
    if (!j->committed) {
        target = &ramdisk[j->target_block * RAMDISK_BLOCK_SIZE];
        copy_bytes(target, old_data, RAMDISK_BLOCK_SIZE);
    }

    journal_clear();
}

void ramdisk_init(void)
{
    /* Recover first, even if the superblock itself was the interrupted target. */
    journal_recover();

    /* fs.c owns formatting and writes the filesystem superblock. */
}

uint8_t *ramdisk_data(void)
{
    return ramdisk;
}

void ramdisk_read_block(uint32_t block, void *buf)
{
    uint8_t *src;
    uint8_t *dst;
    uint32_t i;

    if (block >= RAMDISK_BLOCKS || !buf) return;
    src = &ramdisk[block * RAMDISK_BLOCK_SIZE];
    dst = (uint8_t *)buf;
    for (i = 0; i < RAMDISK_BLOCK_SIZE; i++) dst[i] = src[i];
}

int ramdisk_write_block(uint32_t block, const void *buf)
{
    journal_header_t *j;
    uint8_t *target;
    uint8_t *old_data;
    const uint8_t *src;
    uint32_t i;

    if (block >= RAMDISK_BLOCKS || !buf) return -1;
    if (block == JOURNAL_BLOCK || block == JOURNAL_DATA_BLOCK) return -1;

    j = (journal_header_t *)&ramdisk[JOURNAL_BLOCK * RAMDISK_BLOCK_SIZE];
    old_data = &ramdisk[JOURNAL_DATA_BLOCK * RAMDISK_BLOCK_SIZE];
    target = &ramdisk[block * RAMDISK_BLOCK_SIZE];
    src = (const uint8_t *)buf;

    /* WAL sequence:
     * 1. write before-image + journal header
     * 2. write target
     * 3. mark committed
     * 4. clear journal
     *
     * If execution stops between 2 and 3, init() restores the before-image. */
    copy_bytes(old_data, target, RAMDISK_BLOCK_SIZE);
    j->magic = JOURNAL_MAGIC;
    j->target_block = block;
    j->checksum = checksum32(old_data, RAMDISK_BLOCK_SIZE);
    j->committed = 0;

    for (i = 0; i < RAMDISK_BLOCK_SIZE; i++) target[i] = src[i];

    j->committed = 1;
    journal_clear();
    return 0;
}

void ramdisk_zero_block(uint32_t block)
{
    uint8_t zero[RAMDISK_BLOCK_SIZE];
    uint32_t i;
    for (i = 0; i < RAMDISK_BLOCK_SIZE; i++) zero[i] = 0;
    (void)ramdisk_write_block(block, zero);
}
