/* =============================================================================
 * SENG21213-OS :: Process management - Stage 1
 * Fixed 4 KiB process stacks are carved from a reserved low-memory pool.
 * This avoids malloc and keeps the Stage 0 memory stub untouched.
 * ============================================================================= */
#include "process.h"
#include "vga.h"

extern void scheduler_block_current(uint32_t wake_tick);
#define STACK_POOL_END (PROCESS_STACK_POOL + (MAX_PROCESSES * STACK_SIZE))

static pcb_t pcbs[MAX_PROCESSES];
static uint32_t next_pid = 1;
static uint32_t next_tid = 1;
static uint32_t process_total = 0;
static pcb_t *current = (pcb_t *)0;

extern void scheduler_yield_entry(void);
extern void process_decrement_count(void);

static void k_name(char *dst, const char *src)
{
    uint32_t i = 0;
    if (!src) src = "process";
    while (src[i] && i < PROCESS_NAME_LEN - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void process_bootstrap(void)
{
    if (current && current->is_thread && current->thread_entry)
        current->thread_entry(current->thread_arg);
    else if (current && current->entry)
        current->entry();
    process_exit();
    for (;;) __asm__ __volatile__("hlt");
}

static void build_initial_context(pcb_t *p)
{
    uint32_t *sp = (uint32_t *)p->stack_top;

    /* iret frame: EIP, CS, EFLAGS. */
    *(--sp) = 0x00000202;              /* IF=1 */
    *(--sp) = 0x00000008;              /* kernel code selector */
    *(--sp) = (uint32_t)process_bootstrap;

    /* PUSHAD image: EDI, ESI, EBP, saved ESP, EBX, EDX, ECX, EAX. */
    *(--sp) = 0; /* EDI */
    *(--sp) = 0; /* ESI */
    *(--sp) = 0; /* EBP */
    *(--sp) = 0; /* saved ESP slot is ignored by POPAD */
    *(--sp) = 0; /* EBX */
    *(--sp) = 0; /* EDX */
    *(--sp) = 0; /* ECX */
    *(--sp) = 0; /* EAX */

    p->esp = (uint32_t)sp;
    p->eip = (uint32_t)process_bootstrap;
}

void process_init(void)
{
    uint32_t i;
    for (i = 0; i < MAX_PROCESSES; i++) {
        pcbs[i].pid = 0;
        pcbs[i].state = PROC_TERMINATED;
        pcbs[i].esp = 0;
        pcbs[i].eip = 0;
        pcbs[i].stack_base = 0;
        pcbs[i].stack_top = 0;
        pcbs[i].wake_tick = 0;
        pcbs[i].priority = 0;
        pcbs[i].base_priority = 0;
        pcbs[i].quantum_ticks = 0;
        pcbs[i].ticks_used = 0;
        pcbs[i].has_stack = 0;
        pcbs[i].is_thread = 0;
        pcbs[i].tid = 0;
        pcbs[i].parent = (pcb_t *)0;
        pcbs[i].thread_entry = (thread_entry_t)0;
        pcbs[i].thread_arg = (void *)0;
        pcbs[i].owned_mutexes = 0;
        pcbs[i].waiting_mutex = (struct mutex *)0;
        pcbs[i].rw_read_locks = 0;
        pcbs[i].entry = (process_entry_t)0;
        pcbs[i].name[0] = '\0';
    }

    /* PCB 0 represents the already-running Stage 0 shell. */
    pcbs[0].pid = 0;
    pcbs[0].state = PROC_RUNNING;
    pcbs[0].priority = 0;
    pcbs[0].base_priority = 0;
    pcbs[0].quantum_ticks = 4;
    pcbs[0].is_thread = 0;
    pcbs[0].tid = 0;
    pcbs[0].parent = (pcb_t *)0;
    pcbs[0].entry = (process_entry_t)0;
    k_name(pcbs[0].name, "shell");
    current = &pcbs[0];
    process_total = 1;
    next_pid = 1;
    next_tid = 1;

    /* Do not let this pool overlap the 1 MB area used by Stage 0. */
    (void)STACK_POOL_END;
}

pcb_t *process_create_named(process_entry_t entry, const char *name)
{
    uint32_t i;
    pcb_t *p = (pcb_t *)0;

    if (!entry) return (pcb_t *)0;

    for (i = 1; i < MAX_PROCESSES; i++) {
        if (pcbs[i].state == PROC_TERMINATED && !pcbs[i].has_stack) {
            p = &pcbs[i];
            break;
        }
    }
    if (!p) return (pcb_t *)0;

    p->pid = next_pid++;
    p->state = PROC_READY;
    p->priority = 0;
    p->base_priority = 0;
    p->quantum_ticks = 4;
    p->ticks_used = 0;
    p->wake_tick = 0;
    p->entry = entry;
    p->is_thread = 0;
    p->tid = 0;
    p->parent = (pcb_t *)0;
    p->thread_entry = (thread_entry_t)0;
    p->thread_arg = (void *)0;
    p->owned_mutexes = 0;
    p->waiting_mutex = (struct mutex *)0;
    p->rw_read_locks = 0;
    p->stack_base = PROCESS_STACK_POOL + (i * STACK_SIZE);
    p->stack_top = p->stack_base + STACK_SIZE;
    p->has_stack = 1;
    k_name(p->name, name);
    build_initial_context(p);
    process_total++;
    return p;
}

thread_t *thread_create_named(thread_entry_t entry, void *arg, const char *name)
{
    uint32_t i;
    pcb_t *p = (pcb_t *)0;
    pcb_t *parent = process_current();

    if (!entry || !parent) return (thread_t *)0;

    for (i = 1; i < MAX_PROCESSES; i++) {
        if (pcbs[i].state == PROC_TERMINATED && !pcbs[i].has_stack) {
            p = &pcbs[i];
            break;
        }
    }
    if (!p) return (thread_t *)0;

    p->pid = next_pid++;
    p->tid = next_tid++;
    p->state = PROC_READY;
    p->priority = parent->priority;
    p->base_priority = parent->base_priority;
    p->quantum_ticks = 4;
    p->ticks_used = 0;
    p->wake_tick = 0;
    p->entry = (process_entry_t)0;
    p->is_thread = 1;
    p->parent = parent;
    p->thread_entry = entry;
    p->thread_arg = arg;
    p->owned_mutexes = 0;
    p->waiting_mutex = (struct mutex *)0;
    p->rw_read_locks = 0;
    p->stack_base = PROCESS_STACK_POOL + (i * STACK_SIZE);
    p->stack_top = p->stack_base + STACK_SIZE;
    p->has_stack = 1;
    k_name(p->name, name ? name : "thread");
    build_initial_context(p);
    process_total++;
    return p;
}

void thread_yield(void)
{
    __asm__ __volatile__("int $0x30" ::: "memory");
}

pcb_t *create_process(process_entry_t entry)
{
    return process_create_named(entry, "process");
}

void process_exit(void)
{
    if (!current || current->pid == 0) return;
    current->state = PROC_TERMINATED;
    current->entry = (process_entry_t)0;
    current->thread_entry = (thread_entry_t)0;
    current->thread_arg = (void *)0;
    current->waiting_mutex = (struct mutex *)0;
    current->esp = 0;
    current->eip = 0;
    current->has_stack = 0;
    process_decrement_count();
}

void sleep(uint32_t ms)
{
    extern uint32_t scheduler_ticks(void);
    uint32_t ticks = (ms + 9) / 10;
    if (!current || current->pid == 0 || ticks == 0) {
        if (ticks == 0) return;
        return;
    }
    current->wake_tick = scheduler_ticks() + ticks;
scheduler_block_current(current->wake_tick);
__asm__ __volatile__("int $0x30");
}

/* These wrappers are software-interrupt based so fork/kill see a complete
 * PUSHAD register frame and can safely clone/modify the running context. */
int fork(void)
{
    int result;
    __asm__ __volatile__("int $0x31" : "=a"(result) : : "memory");
    return result;
}

int kill(uint32_t pid)
{
    int result;
    __asm__ __volatile__("int $0x32" : "=a"(result) : "a"(pid) : "memory");
    return result;
}

pcb_t *process_current(void) { return current; }
void process_set_current(pcb_t *p) { current = p; }
pcb_t *process_get(uint32_t pid)
{
    uint32_t i;
    for (i = 0; i < MAX_PROCESSES; i++)
        if (pcbs[i].pid == pid && pcbs[i].state != PROC_TERMINATED)
            return &pcbs[i];
    return (pcb_t *)0;
}
const pcb_t *process_table(void) { return pcbs; }
uint32_t process_count(void) { return process_total; }

const char *process_state_name(proc_state_t state)
{
    switch (state) {
        case PROC_READY: return "READY";
        case PROC_RUNNING: return "RUNNING";
        case PROC_BLOCKED: return "BLOCKED";
        case PROC_TERMINATED: return "TERMINATED";
        default: return "UNKNOWN";
    }
}

/* Internal scheduler accessors. */
pcb_t *process_table_mutable(void) { return pcbs; }
void process_increment_count(void)
{
    if (process_total < MAX_PROCESSES) process_total++;
}

uint32_t process_allocate_pid(void)
{
    return next_pid++;
}

void process_decrement_count(void)
{
    if (process_total) process_total--;
}
