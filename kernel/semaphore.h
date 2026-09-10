#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include "process.h"

#define SEM_MAX_WAITERS MAX_PROCESSES

typedef struct semaphore {
    int32_t count;
    uint8_t initialized;
    pcb_t *waiters[SEM_MAX_WAITERS];
    uint32_t waiter_count;
} semaphore_t;

typedef semaphore_t sem_t;

void semaphore_init(semaphore_t *s, int32_t initial_count);
void sem_init(semaphore_t *s, int32_t initial_count);
void sem_wait(semaphore_t *s);
void sem_signal(semaphore_t *s);

#endif
