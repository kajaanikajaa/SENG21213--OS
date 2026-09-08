/*
 * Stage 2 demonstrations from Lecture 10.
 *
 * The demos are deliberately deterministic enough for a shell-based viva:
 * each test waits for its worker threads to finish before printing PASS/FAIL.
 */
#include "stage2_demo.h"
#include "thread.h"
#include "mutex.h"
#include "semaphore.h"
#include "rwlock.h"
#include "deadlock.h"
#include "scheduler.h"
#include "vga.h"

#define TEST_ITEMS 20
#define BUFFER_SIZE 5
#define TEST_TIMEOUT_TICKS 300

static volatile uint32_t demo_done;
static volatile uint32_t demo_error;
static volatile int32_t shared_value;
static mutex_t demo_mutex;

static void wait_until(volatile uint32_t *flag)
{
    uint32_t start = scheduler_ticks();
    while (!*flag && scheduler_ticks() - start < TEST_TIMEOUT_TICKS)
        thread_yield();
}

static void thread_counter(void *arg)
{
    uint32_t *counter = (uint32_t *)arg;
    uint32_t i;
    for (i = 0; i < 5; i++) {
        (*counter)++;
        thread_yield();
    }
    demo_done++;
}

void stage2_thread_demo(void)
{
    uint32_t a = 0, b = 0;
    demo_done = 0;
    demo_error = 0;
    if (!thread_create_named(thread_counter, &a, "thread-A") ||
        !thread_create_named(thread_counter, &b, "thread-B")) {
        vga_puts("\n[Threads] FAIL: thread creation failed.\n");
        return;
    }
    wait_until(&demo_done);
    if (demo_done == 2 && a == 5 && b == 5)
        vga_puts("\n[Threads] PASS: two kernel threads shared the process address space.\n");
    else
        vga_puts("\n[Threads] FAIL: thread execution did not complete correctly.\n");
}

static void race_worker(void *arg)
{
    uint32_t safe = (uint32_t)arg;
    uint32_t i;
    for (i = 0; i < TEST_ITEMS; i++) {
        int32_t local;
        if (safe) mutex_lock(&demo_mutex);
        local = shared_value;
        if (!safe) thread_yield();
        local++;
        if (!safe) thread_yield();
        shared_value = local;
        if (safe) mutex_unlock(&demo_mutex);
        thread_yield();
    }
    demo_done++;
}

static int run_race_case(uint32_t safe)
{
    shared_value = 0;
    demo_done = 0;
    mutex_init(&demo_mutex);
    if (!thread_create_named(race_worker, (void *)safe, safe ? "race-safe-A" : "race-unsafe-A") ||
        !thread_create_named(race_worker, (void *)safe, safe ? "race-safe-B" : "race-unsafe-B"))
        return -1;
    wait_until(&demo_done);
    return shared_value;
}

void stage2_race_demo(void)
{
    int unsafe = run_race_case(0);
    int safe = run_race_case(1);
    vga_puts("\n[Race] Lecture 10 myglobal demonstration\n");
    if (unsafe < 0 || safe < 0) {
        vga_puts("  FAIL: could not create race-test threads.\n");
        return;
    }
    vga_printf("  Without mutex : myglobal = %d (expected 40; lost updates demonstrate the race)\n", unsafe);
    vga_printf("  With mutex    : myglobal = %d (expected 40)\n", safe);
    if (safe == 40 && unsafe < 40)
        vga_puts("[Race] PASS: mutex removed the lost-update race.\n");
    else
        vga_puts("[Race] FAIL: race demonstration was inconclusive.\n");
}

static int buffer[BUFFER_SIZE];
static uint32_t in_pos, out_pos, consumed;
static semaphore_t empty_slots, full_slots, buffer_mutex;

static void producer(void *arg)
{
    uint32_t i;
    (void)arg;
    for (i = 1; i <= TEST_ITEMS; i++) {
        sem_wait(&empty_slots);
        sem_wait(&buffer_mutex);
        buffer[in_pos] = (int)i;
        in_pos = (in_pos + 1) % BUFFER_SIZE;
        sem_signal(&buffer_mutex);
        sem_signal(&full_slots);
        thread_yield();
    }
    demo_done++;
}

static void consumer(void *arg)
{
    uint32_t expected = 1;
    (void)arg;
    while (consumed < TEST_ITEMS) {
        int value;
        sem_wait(&full_slots);
        sem_wait(&buffer_mutex);
        value = buffer[out_pos];
        out_pos = (out_pos + 1) % BUFFER_SIZE;
        sem_signal(&buffer_mutex);
        sem_signal(&empty_slots);
        if (value != (int)expected) demo_error = 1;
        expected++;
        consumed++;
        thread_yield();
    }
    demo_done++;
}

void stage2_producer_consumer_demo(void)
{
    uint32_t start;
    in_pos = out_pos = consumed = 0;
    demo_done = 0;
    demo_error = 0;
    semaphore_init(&empty_slots, BUFFER_SIZE);
    semaphore_init(&full_slots, 0);
    semaphore_init(&buffer_mutex, 1);
    if (!thread_create_named(producer, 0, "producer") ||
        !thread_create_named(consumer, 0, "consumer")) {
        vga_puts("\n[Producer-Consumer] FAIL: thread creation failed.\n");
        return;
    }
    start = scheduler_ticks();
    while (demo_done < 2 && scheduler_ticks() - start < TEST_TIMEOUT_TICKS)
        thread_yield();
    vga_puts("\n[Producer-Consumer] Bounded buffer size = 5\n");
    vga_printf("  Produced/consumed items = %u\n", consumed);
    if (demo_done == 2 && consumed == TEST_ITEMS && !demo_error)
        vga_puts("[Producer-Consumer] PASS: semaphores prevented buffer corruption.\n");
    else
        vga_puts("[Producer-Consumer] FAIL: buffer test did not complete cleanly.\n");
}

static pcb_t *pi_low;
static volatile uint32_t pi_owner_started;
static volatile uint32_t pi_high_done;
static volatile uint32_t pi_boost_seen;

static void pi_low_owner(void *arg)
{
    uint32_t i;
    (void)arg;
    mutex_lock(&demo_mutex);
    pi_owner_started = 1;
    for (i = 0; i < 20; i++) thread_yield();
    mutex_unlock(&demo_mutex);
}

static void pi_high_waiter(void *arg)
{
    (void)arg;
    mutex_lock(&demo_mutex);
    pi_high_done = 1;
    mutex_unlock(&demo_mutex);
}

void stage2_priority_demo(void)
{
    pcb_t *hi;
    uint32_t start;
    pi_owner_started = pi_high_done = pi_boost_seen = 0;
    mutex_init(&demo_mutex);
    pi_low = thread_create_named(pi_low_owner, 0, "low-owner");
    if (!pi_low) { vga_puts("\n[Priority] FAIL: low-priority owner creation failed.\n"); return; }

    /* Let the owner acquire the mutex while it still has normal priority. */
    start = scheduler_ticks();
    while (!pi_owner_started && scheduler_ticks() - start < TEST_TIMEOUT_TICKS) thread_yield();
    if (!pi_owner_started) { vga_puts("\n[Priority] FAIL: owner did not acquire mutex.\n"); return; }

    /* Lower the owner after acquisition, then introduce a high-priority waiter. */
    pi_low->base_priority = 2;
    pi_low->priority = 2;
    hi = thread_create_named(pi_high_waiter, 0, "high-wait");
    if (!hi) { vga_puts("[Priority] FAIL: high-priority waiter creation failed.\n"); return; }
    hi->base_priority = 0;
    hi->priority = 0;

    start = scheduler_ticks();
    while (scheduler_ticks() - start < TEST_TIMEOUT_TICKS) {
        if (pi_low->priority == hi->priority) { pi_boost_seen = 1; break; }
        thread_yield();
    }
    if (!pi_boost_seen) {
        vga_printf("  FAIL: owner priority stayed at %u\n", pi_low->priority);
        return;
    }
    vga_printf("\n[Priority] Inheritance observed: owner effective priority = %u\n", pi_low->priority);
    start = scheduler_ticks();
    while (!pi_high_done && scheduler_ticks() - start < TEST_TIMEOUT_TICKS) thread_yield();
    if (pi_high_done && pi_boost_seen)
        vga_puts("[Priority] PASS: priority inheritance prevented inversion.\n");
    else
        vga_puts("[Priority] FAIL: high-priority waiter did not complete.\n");
}

static rwlock_t demo_rw;
static volatile uint32_t rw_readers_seen;
static volatile uint32_t rw_writer_seen;
static int rw_shared;

static void rw_reader(void *arg)
{
    (void)arg;
    rwlock_read_lock(&demo_rw);
    rw_readers_seen++;
    thread_yield();
    rwlock_read_unlock(&demo_rw);
    demo_done++;
}

static void rw_writer(void *arg)
{
    (void)arg;
    rwlock_write_lock(&demo_rw);
    rw_shared++;
    rw_writer_seen = 1;
    rwlock_write_unlock(&demo_rw);
    demo_done++;
}

void stage2_rwlock_demo(void)
{
    rwlock_init(&demo_rw);
    rw_readers_seen = rw_writer_seen = rw_shared = demo_done = 0;
    if (!thread_create_named(rw_reader, 0, "reader-A") ||
        !thread_create_named(rw_reader, 0, "reader-B") ||
        !thread_create_named(rw_writer, 0, "writer")) {
        vga_puts("\n[RWLock] FAIL: thread creation failed.\n");
        return;
    }
    wait_until(&demo_done);
    if (demo_done == 3 && rw_readers_seen == 2 && rw_writer_seen && rw_shared == 1)
        vga_puts("\n[RWLock] PASS: multiple readers shared access and writer was exclusive.\n");
    else
        vga_puts("\n[RWLock] FAIL: reader-writer lock test failed.\n");
}

void stage2_deadlock_demo(void)
{
    int runtime_cycle = deadlock_detect();
    int synthetic_cycle = deadlock_demo();
    vga_puts("\n[Deadlock] Resource-allocation graph DFS demonstration\n");
    vga_printf("  Current mutex graph: %s\n", runtime_cycle ? "cycle detected" : "no cycle detected");
    if (synthetic_cycle)
        vga_puts("  Synthetic cycle test: detected\n[Deadlock] PASS: DFS deadlock detector works.\n");
    else
        vga_puts("[Deadlock] FAIL: DFS did not detect the synthetic cycle.\n");
}
