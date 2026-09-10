#ifndef MUTEX_H
#define MUTEX_H

#include "process.h"

#define MUTEX_MAX_WAITERS MAX_PROCESSES
#define MUTEX_MAX_OWNERS  4

typedef struct mutex {
    uint8_t locked;
    uint8_t initialized;
    pcb_t *owner;
    pcb_t *waiters[MUTEX_MAX_WAITERS];
    uint32_t waiter_count;
} mutex_t;

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);
int mutex_is_locked(const mutex_t *m);

#endif
