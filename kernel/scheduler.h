#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "process.h"

void scheduler_init(void);
void scheduler_start(void);
void scheduler_block_current(uint32_t wake_tick);
void scheduler_block_forever(void);
void scheduler_wake(pcb_t *p);
uint32_t scheduler_tick(uint32_t *context_esp);
uint32_t scheduler_yield(uint32_t *context_esp);
uint32_t scheduler_fork(uint32_t *context_esp);
uint32_t scheduler_kill(uint32_t *context_esp, uint32_t pid);
uint32_t scheduler_ticks(void);

#endif
