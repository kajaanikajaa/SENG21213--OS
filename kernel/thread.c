/*
 * Stage 2 thread interface.
 * Lecture 10: kernel thread creation using thread_create(fn, arg).
 */
#include "thread.h"

thread_t *thread_create(thread_entry_t entry, void *arg)
{
    return thread_create_named(entry, arg, "thread");
}
