#ifndef THREAD_H
#define THREAD_H

#include "process.h"

/* A Stage 2 kernel thread is represented by a scheduler PCB with its own
 * execution stack and a parent PCB. The address-space model is shared because
 * Stage 1 has a single kernel address space. */

thread_t *thread_create(thread_entry_t entry, void *arg);
thread_t *thread_create_named(thread_entry_t entry, void *arg, const char *name);
void thread_yield(void);

#endif
