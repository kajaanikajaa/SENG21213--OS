/*
 * L10 counting semaphore.
 *
 * A semaphore represents a number of available units. A waiter that cannot
 * obtain a unit blocks; sem_signal() returns a unit or wakes one waiter.
 * The implementation uses the same scheduler BLOCKED/READY states as the
 * mutex, so waiting does not busy-spin.
 */
#include "semaphore.h"
#include "scheduler.h"

static uint32_t irq_save(void)
{
    uint32_t flags;
    __asm__ __volatile__("pushfl; popl %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static void irq_restore(uint32_t flags)
{
    __asm__ __volatile__("pushl %0; popfl" :: "r"(flags) : "memory");
}

static void waitq_add(semaphore_t *s, pcb_t *p)
{
    uint32_t i;
    if (s->waiter_count >= SEM_MAX_WAITERS) return;
    for (i = 0; i < s->waiter_count; i++)
        if (s->waiters[i] == p) return;
    s->waiters[s->waiter_count++] = p;
}

static pcb_t *waitq_pop(semaphore_t *s)
{
    uint32_t i;
    pcb_t *p;
    if (!s->waiter_count) return (pcb_t *)0;
    p = s->waiters[0];
    for (i = 1; i < s->waiter_count; i++)
        s->waiters[i - 1] = s->waiters[i];
    s->waiter_count--;
    s->waiters[s->waiter_count] = (pcb_t *)0;
    return p;
}

void semaphore_init(semaphore_t *s, int32_t initial_count)
{
    uint32_t i;
    if (!s) return;
    s->count = initial_count < 0 ? 0 : initial_count;
    s->initialized = 1;
    s->waiter_count = 0;
    for (i = 0; i < SEM_MAX_WAITERS; i++) s->waiters[i] = (pcb_t *)0;
}

void sem_init(semaphore_t *s, int32_t initial_count)
{
    semaphore_init(s, initial_count);
}

void sem_wait(semaphore_t *s)
{
    pcb_t *cur = process_current();
    uint32_t flags;

    if (!s || !cur || cur->pid == 0) return;
    if (!s->initialized) semaphore_init(s, 0);

    for (;;) {
        flags = irq_save();
        if (s->count > 0) {
            s->count--;
            irq_restore(flags);
            return;
        }

        if (s->waiter_count < SEM_MAX_WAITERS) {
            waitq_add(s, cur);
            scheduler_block_forever();
            irq_restore(flags);
            thread_yield();
            /* sem_signal() wakes exactly one queued waiter. */
            if (cur->state != PROC_BLOCKED) return;
        } else {
            irq_restore(flags);
            thread_yield();
        }
    }
}

void sem_signal(semaphore_t *s)
{
    pcb_t *next;
    uint32_t flags;

    if (!s) return;
    if (!s->initialized) semaphore_init(s, 0);

    flags = irq_save();
    next = waitq_pop(s);
    if (next) {
        scheduler_wake(next);
    } else {
        s->count++;
    }
    irq_restore(flags);
}
