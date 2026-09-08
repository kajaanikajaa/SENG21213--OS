/*
 * L10 synchronization primitive: a blocking mutex.
 *
 * The important difference from a spinlock is that a thread waiting for a
 * locked mutex is moved to BLOCKED instead of consuming CPU time in a loop.
 * The owner also inherits the highest urgency (lowest numeric priority) of
 * its waiters. This prevents the classic priority-inversion case discussed
 * in Lecture 10.
 */
#include "mutex.h"
#include "scheduler.h"

#define MUTEX_REGISTRY_SIZE 32
static mutex_t *mutex_registry[MUTEX_REGISTRY_SIZE];
static uint32_t mutex_registry_used;

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

static int waitq_add(mutex_t *m, pcb_t *p)
{
    uint32_t i;
    if (m->waiter_count >= MUTEX_MAX_WAITERS) return 0;
    for (i = 0; i < m->waiter_count; i++)
        if (m->waiters[i] == p) return 1;
    m->waiters[m->waiter_count++] = p;
    return 1;
}

static pcb_t *waitq_pop(mutex_t *m)
{
    uint32_t i;
    pcb_t *p;
    if (m->waiter_count == 0) return (pcb_t *)0;
    p = m->waiters[0];
    for (i = 1; i < m->waiter_count; i++)
        m->waiters[i - 1] = m->waiters[i];
    m->waiters[m->waiter_count - 1] = (pcb_t *)0;
    m->waiter_count--;
    return p;
}

static void owner_add(pcb_t *p, mutex_t *m)
{
    uint32_t i;
    if (!p || p->owned_mutexes >= MUTEX_MAX_OWNERS) return;
    for (i = 0; i < p->owned_mutexes; i++)
        if (p->owned_mutex_list[i] == m) return;
    p->owned_mutex_list[p->owned_mutexes++] = m;
}

static void owner_remove(pcb_t *p, mutex_t *m)
{
    uint32_t i;
    if (!p) return;
    for (i = 0; i < p->owned_mutexes; i++) {
        if (p->owned_mutex_list[i] == m) {
            for (; i + 1 < p->owned_mutexes; i++)
                p->owned_mutex_list[i] = p->owned_mutex_list[i + 1];
            p->owned_mutex_list[p->owned_mutexes - 1] = (struct mutex *)0;
            p->owned_mutexes--;
            return;
        }
    }
}

static void recompute_priority(pcb_t *p)
{
    uint32_t i, j;
    if (!p) return;
    p->priority = p->base_priority;
    for (i = 0; i < p->owned_mutexes; i++) {
        mutex_t *m = p->owned_mutex_list[i];
        if (!m) continue;
        for (j = 0; j < m->waiter_count; j++) {
            pcb_t *w = m->waiters[j];
            if (w && w->priority < p->priority)
                p->priority = w->priority;
        }
    }
}

static void propagate_priority(pcb_t *p)
{
    uint32_t guard = 0;
    while (p && p->waiting_mutex && p->waiting_mutex->owner && guard++ < MAX_PROCESSES) {
        pcb_t *owner = p->waiting_mutex->owner;
        if (p->priority >= owner->priority) break;
        owner->priority = p->priority;
        p = owner;
    }
}

void mutex_init(mutex_t *m)
{
    uint32_t i;
    if (!m) return;
    m->locked = 0;
    m->initialized = 1;
    m->owner = (pcb_t *)0;
    m->waiter_count = 0;
    for (i = 0; i < MUTEX_MAX_WAITERS; i++) m->waiters[i] = (pcb_t *)0;
    for (i = 0; i < mutex_registry_used; i++)
        if (mutex_registry[i] == m) return;
    if (mutex_registry_used < MUTEX_REGISTRY_SIZE)
        mutex_registry[mutex_registry_used++] = m;
}

uint32_t mutex_registry_count(void) { return mutex_registry_used; }
mutex_t *mutex_registry_get(uint32_t index)
{
    return index < mutex_registry_used ? mutex_registry[index] : (mutex_t *)0;
}

int mutex_is_locked(const mutex_t *m)
{
    return m && m->locked;
}

void mutex_lock(mutex_t *m)
{
    pcb_t *cur = process_current();
    uint32_t flags;

    if (!m || !cur || cur->pid == 0) return;
    if (!m->initialized) mutex_init(m);
    if (m->owner == cur) return;

    for (;;) {
        flags = irq_save();
        if (!m->locked) {
            m->locked = 1;
            m->owner = cur;
            owner_add(cur, m);
            irq_restore(flags);
            return;
        }

        if (!waitq_add(m, cur)) {
            irq_restore(flags);
            thread_yield();
            continue;
        }

        cur->waiting_mutex = m;
        /* L10 priority inheritance: boost the lock holder immediately. */
        if (m->owner && cur->priority < m->owner->priority) {
            m->owner->priority = cur->priority;
            propagate_priority(m->owner);
        }
        scheduler_block_forever();
        irq_restore(flags);

        /* Context switch away. mutex_unlock transfers ownership to us. */
        thread_yield();
        if (m->owner == cur) {
            cur->waiting_mutex = (struct mutex *)0;
            return;
        }
    }
}

void mutex_unlock(mutex_t *m)
{
    pcb_t *cur = process_current();
    pcb_t *next;
    uint32_t flags;

    if (!m || !cur || m->owner != cur) return;

    flags = irq_save();
    owner_remove(cur, m);
    next = waitq_pop(m);

    if (next) {
        m->locked = 1;
        m->owner = next;
        next->waiting_mutex = (struct mutex *)0;
        owner_add(next, m);
        scheduler_wake(next);
        recompute_priority(cur);
    } else {
        m->locked = 0;
        m->owner = (pcb_t *)0;
        recompute_priority(cur);
    }
    irq_restore(flags);
}
