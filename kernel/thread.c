/*
 * Stage 2 thread interface.
 *
 * Thread creation is implemented by process_create_thread-style PCB creation
 * in process.c so existing Stage 1 scheduling and context-switch machinery is
 * reused. This file keeps the Lecture 10 thread API in a dedicated module.
 */
#include "thread.h"

/* The actual allocation lives in process.c; these wrappers keep the public
 * thread API small and avoid introducing a heap into the Stage 0/1 kernel. */
thread_t *thread_create(thread_entry_t entry, void *arg)
{
    return thread_create_named(entry, arg, "thread");
}
