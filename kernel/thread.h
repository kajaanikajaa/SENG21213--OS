#ifndef THREAD_H
#define THREAD_H

#include "process.h"



thread_t *thread_create(thread_entry_t entry, void *arg);
thread_t *thread_create_named(thread_entry_t entry, void *arg, const char *name);
void thread_yield(void);

#endif
