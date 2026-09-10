#ifndef SLAB_H
#define SLAB_H

#include "../include/types.h"

void slab_init(void);

void *kmalloc(uint32_t size);
void kfree(void *ptr);

#endif