#ifndef RWLOCK_H
#define RWLOCK_H

#include "process.h"

#define RW_MAX_WAITERS MAX_PROCESSES

typedef struct rwlock {
    uint32_t readers;
    pcb_t *writer;
    pcb_t *reader_waiters[RW_MAX_WAITERS];
    pcb_t *writer_waiters[RW_MAX_WAITERS];
    uint32_t reader_waiter_count;
    uint32_t writer_waiter_count;
    uint8_t initialized;
} rwlock_t;

void rwlock_init(rwlock_t *rw);
void rwlock_read_lock(rwlock_t *rw);
void rwlock_read_unlock(rwlock_t *rw);
void rwlock_write_lock(rwlock_t *rw);
void rwlock_write_unlock(rwlock_t *rw);

#endif
