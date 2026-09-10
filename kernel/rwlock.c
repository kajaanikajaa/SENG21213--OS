/*
 * L10 concurrency pattern: reader-writer lock.
 *
 * Multiple readers may enter together. A writer needs exclusive access, and
 * new readers are held back while a writer is waiting to avoid writer
 * starvation. Waiters are blocked rather than spin-waiting.
 */
#include "rwlock.h"
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

static void add(pcb_t **q, uint32_t *count, pcb_t *p)
{
    uint32_t i;
    if (*count >= RW_MAX_WAITERS) return;
    for (i = 0; i < *count; i++) if (q[i] == p) return;
    q[(*count)++] = p;
}

static pcb_t *pop(pcb_t **q, uint32_t *count)
{
    uint32_t i;
    pcb_t *p;
    if (!*count) return (pcb_t *)0;
    p = q[0];
    for (i = 1; i < *count; i++) q[i - 1] = q[i];
    q[--(*count)] = (pcb_t *)0;
    return p;
}

void rwlock_init(rwlock_t *rw)
{
    uint32_t i;
    if (!rw) return;
    rw->readers = 0;
    rw->writer = (pcb_t *)0;
    rw->reader_waiter_count = 0;
    rw->writer_waiter_count = 0;
    rw->initialized = 1;
    for (i = 0; i < RW_MAX_WAITERS; i++) {
        rw->reader_waiters[i] = (pcb_t *)0;
        rw->writer_waiters[i] = (pcb_t *)0;
    }
}

void rwlock_read_lock(rwlock_t *rw)
{
    pcb_t *cur = process_current();
    uint32_t flags;
    if (!rw || !cur || cur->pid == 0) return;
    if (!rw->initialized) rwlock_init(rw);

    for (;;) {
        flags = irq_save();
        if (!rw->writer && rw->writer_waiter_count == 0) {
            rw->readers++;
            cur->rw_read_locks++;
            irq_restore(flags);
            return;
        }
        add(rw->reader_waiters, &rw->reader_waiter_count, cur);
        scheduler_block_forever();
        irq_restore(flags);
        thread_yield();
        if (cur->state != PROC_BLOCKED) return;
    }
}

void rwlock_read_unlock(rwlock_t *rw)
{
    pcb_t *next;
    uint32_t flags;
    pcb_t *cur = process_current();
    if (!rw || !cur || cur->rw_read_locks == 0) return;

    flags = irq_save();
    cur->rw_read_locks--;
    if (rw->readers) rw->readers--;
    if (rw->readers == 0 && !rw->writer) {
        next = pop(rw->writer_waiters, &rw->writer_waiter_count);
        if (next) scheduler_wake(next);
    }
    irq_restore(flags);
}

void rwlock_write_lock(rwlock_t *rw)
{
    pcb_t *cur = process_current();
    uint32_t flags;
    if (!rw || !cur || cur->pid == 0) return;
    if (!rw->initialized) rwlock_init(rw);

    for (;;) {
        flags = irq_save();
        if (!rw->writer && rw->readers == 0) {
            rw->writer = cur;
            irq_restore(flags);
            return;
        }
        add(rw->writer_waiters, &rw->writer_waiter_count, cur);
        scheduler_block_forever();
        irq_restore(flags);
        thread_yield();
        if (rw->writer == cur) return;
    }
}

void rwlock_write_unlock(rwlock_t *rw)
{
    pcb_t *next;
    uint32_t flags;
    pcb_t *r;
    if (!rw || rw->writer != process_current()) return;

    flags = irq_save();
    rw->writer = (pcb_t *)0;
    next = pop(rw->writer_waiters, &rw->writer_waiter_count);
    if (next) {
        rw->writer = next;
        scheduler_wake(next);
    } else {
        /* No writer: release all waiting readers as one reader group. */
        while ((r = pop(rw->reader_waiters, &rw->reader_waiter_count)) != (pcb_t *)0) {
            rw->readers++;
            r->rw_read_locks++;
            scheduler_wake(r);
        }
    }
    irq_restore(flags);
}
