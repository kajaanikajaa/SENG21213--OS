#ifndef RAMDISK_H
#define RAMDISK_H

#include "../include/types.h"

#define RAMDISK_BLOCK_SIZE 4096u
#define RAMDISK_BLOCKS     256u

void ramdisk_init(void);
void ramdisk_read_block(uint32_t block, void *buf);
int  ramdisk_write_block(uint32_t block, const void *buf);
void ramdisk_zero_block(uint32_t block);
uint8_t *ramdisk_data(void);

#endif
