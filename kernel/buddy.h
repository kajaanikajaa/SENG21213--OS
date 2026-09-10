#ifndef BUDDY_H
#define BUDDY_H

#include "../include/types.h"

#define BUDDY_MAX_ORDER 8

void buddy_init(void);
uint32_t buddy_alloc(uint32_t order);
void buddy_free(uint32_t addr, uint32_t order);

#endif