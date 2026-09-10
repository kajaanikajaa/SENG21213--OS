/* =============================================================================
 * SENG21213-OS :: PIT, PIC, IDT and MLFQ/RR scheduler - Stage 1
 * PIT is programmed for 100 Hz. IRQ0 saves the current PUSHAD frame and
 * selects the next runnable PCB. Equal-priority processes are round-robin.
 * ============================================================================= */
#include "scheduler.h"
#include "vga.h"
#include "pmm.h"
#include "../include/types.h"

#define PIT_BASE_HZ 1193182u
#define PIT_HZ 100u
#define PIT_DIVISOR (PIT_BASE_HZ / PIT_HZ)

#define PIC_MASTER_CMD  0x20
#define PIC_MASTER_DATA 0x21
#define PIC_SLAVE_CMD   0xA0
#define PIC_SLAVE_DATA  0xA1
#define PIC_EOI         0x20

#define IDT_IRQ0         32
#define IDT_YIELD        0x30
#define IDT_FORK         0x31
#define IDT_KILL         0x32

#define MLFQ_LEVELS 3
static const uint8_t quantum[MLFQ_LEVELS] = { 4, 8, 16 };


extern void irq0_entry(void);
extern void scheduler_yield_entry(void);
extern void scheduler_fork_entry(void);
extern void scheduler_kill_entry(void);
extern pcb_t *process_table_mutable(void);
extern void process_decrement_count(void);
extern void process_set_current(pcb_t *p);
extern uint32_t process_allocate_pid(void);

struct idt_entry {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;
    uint16_t offset_hi;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static uint32_t tick_count = 0;
static uint32_t rr_cursor = 0;
static uint8_t timer_started = false;
/* Sorted sleep queue: stores PCB indexes ordered by wake_tick. */
static uint32_t sleep_queue[MAX_PROCESSES];
static uint32_t sleep_queue_count = 0;

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void idt_set_gate(uint8_t vector, void (*handler)(void))
{
    uint32_t addr = (uint32_t)handler;
    idt[vector].offset_lo = (uint16_t)(addr & 0xFFFF);
    idt[vector].selector = 0x08;
    idt[vector].zero = 0;
    idt[vector].flags = 0x8E;
    idt[vector].offset_hi = (uint16_t)(addr >> 16);
}

static void idt_init(void)
{
    uint32_t i;
    struct idt_ptr idtp;

    for (i = 0; i < 256; i++) {
        idt[i].offset_lo = 0;
        idt[i].selector = 0;
        idt[i].zero = 0;
        idt[i].flags = 0;
        idt[i].offset_hi = 0;
    }

    idt_set_gate(IDT_IRQ0, irq0_entry);
    idt_set_gate(IDT_YIELD, scheduler_yield_entry);
    idt_set_gate(IDT_FORK, scheduler_fork_entry);
    idt_set_gate(IDT_KILL, scheduler_kill_entry);

    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint32_t)&idt[0];
    __asm__ __volatile__("lidt %0" : : "m"(idtp));
}

static void pic_init(void)
{
    uint8_t master_mask = inb(PIC_MASTER_DATA);
    uint8_t slave_mask = inb(PIC_SLAVE_DATA);

    outb(PIC_MASTER_CMD, 0x11);
    outb(PIC_SLAVE_CMD, 0x11);
    outb(PIC_MASTER_DATA, 0x20);
    outb(PIC_SLAVE_DATA, 0x28);
    outb(PIC_MASTER_DATA, 0x04);
    outb(PIC_SLAVE_DATA, 0x02);
    outb(PIC_MASTER_DATA, 0x01);
    outb(PIC_SLAVE_DATA, 0x01);

    (void)master_mask;
    (void)slave_mask;
    /* IRQ0 only. IRQ1 remains disabled because Stage 0 keyboard is polling. */
    outb(PIC_MASTER_DATA, 0xFE);
    outb(PIC_SLAVE_DATA, 0xFF);
    outb(PIC_MASTER_CMD, PIC_EOI);
}

static void pit_init(void)
{
    uint16_t divisor = (uint16_t)PIT_DIVISOR;
    outb(0x43, 0x36); /* channel 0, lobyte/hibyte, mode 3 */
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)(divisor >> 8));
}

void scheduler_init(void)
{
    process_init();
    idt_init();
    pic_init();
    pit_init();
    tick_count = 0;
    rr_cursor = 0;
    timer_started = false;
    sleep_queue_count = 0;
}

void scheduler_start(void)
{
    timer_started = true;
    __asm__ __volatile__("sti");
}

uint32_t scheduler_ticks(void) { return tick_count; }

static void sleep_queue_insert(pcb_t *p, uint32_t index)
{
    uint32_t pos;

    if (sleep_queue_count >= MAX_PROCESSES)
        return;

    /*
     * Find insertion position.
     * Smaller wake_tick comes first.
     */
    pos = sleep_queue_count;

    while (pos > 0) {
        uint32_t prev = sleep_queue[pos - 1];

        if (p[prev].wake_tick <= p[index].wake_tick)
            break;

        sleep_queue[pos] = prev;
        pos--;
    }

    sleep_queue[pos] = index;
    sleep_queue_count++;
}

void scheduler_block_current(uint32_t wake_tick)
{
    pcb_t *p = process_table_mutable();
    pcb_t *cur = process_current();
    uint32_t i;

    if (!cur || cur->pid == 0)
        return;

    for (i = 0; i < MAX_PROCESSES; i++) {
        if (&p[i] == cur)
            break;
    }

    if (i >= MAX_PROCESSES)
        return;

    cur->wake_tick = wake_tick;
    cur->state = PROC_BLOCKED;

    sleep_queue_insert(p, i);
}

/* Block the current thread indefinitely. Synchronisation primitives use this
 * path because their waiters are awakened explicitly by unlock/signal. */
void scheduler_block_forever(void)
{
    pcb_t *cur = process_current();
    if (!cur || cur->pid == 0) return;
    cur->wake_tick = 0;
    cur->state = PROC_BLOCKED;
}

/* Wake a blocked synchronisation waiter without changing its scheduling policy. */
void scheduler_wake(pcb_t *p)
{
    if (!p || p->state != PROC_BLOCKED) return;
    p->state = PROC_READY;
    p->wake_tick = 0;
    p->ticks_used = 0;
    if (p->quantum_ticks == 0) p->quantum_ticks = 4;
}

static void sleep_queue_remove(uint32_t index)
{
    uint32_t i;

    for (i = 0; i < sleep_queue_count; i++) {
        if (sleep_queue[i] == index) {
            for (; i + 1 < sleep_queue_count; i++)
                sleep_queue[i] = sleep_queue[i + 1];

            sleep_queue_count--;
            return;
        }
    }
}

static void wake_sleepers(pcb_t *p)
{
    while (sleep_queue_count > 0) {
        uint32_t index = sleep_queue[0];
        pcb_t *proc = &p[index];

        /*
         * Queue is sorted, so if the first process
         * has not expired, none of the others have.
         */
        if (proc->wake_tick == 0 ||
            tick_count < proc->wake_tick)
            break;

        /* Remove queue head. */
        sleep_queue_remove(index);

        if (proc->state == PROC_BLOCKED) {
            proc->state = PROC_READY;
            proc->wake_tick = 0;

            /* I/O wake-up boost. */
            proc->priority = 0;
            proc->ticks_used = 0;
            proc->quantum_ticks = quantum[0];
        }
    }
}

static int runnable_at(pcb_t *p, uint32_t i)
{
    return p[i].state == PROC_READY || p[i].state == PROC_RUNNING;
}

static pcb_t *pick_next(pcb_t *p, uint32_t start)
{
    uint32_t pass, i;
    int best_level = MLFQ_LEVELS;

    /* Find the highest non-empty MLFQ level. */
    for (i = 0; i < MAX_PROCESSES; i++) {
        if (runnable_at(p, i) && p[i].priority < (uint8_t)best_level)
            best_level = p[i].priority;
    }
    if (best_level == MLFQ_LEVELS) return (pcb_t *)0;

    /* Round-robin inside that level. */
    for (pass = 0; pass < MAX_PROCESSES; pass++) {
        i = (start + pass) % MAX_PROCESSES;
        if (runnable_at(p, i) && p[i].priority == (uint8_t)best_level)
            return &p[i];
    }
    return (pcb_t *)0;
}

static uint32_t dispatch(uint32_t *context_esp, uint8_t count_tick)
{
    pcb_t *p = process_table_mutable();
    pcb_t *cur = process_current();
    pcb_t *next;
    uint32_t cur_index = 0;
    uint32_t i;

    if (count_tick) {
        tick_count++;
        wake_sleepers(p);
    }

    if (cur) {
        for (i = 0; i < MAX_PROCESSES; i++)
            if (&p[i] == cur) { cur_index = i; break; }

        if (context_esp) {
            /* Save the exact interrupted context even if the process just
             * changed itself to BLOCKED (sleep) before yielding. */
            if (cur->state != PROC_TERMINATED) {
                cur->esp = (uint32_t)context_esp;
                cur->eip = context_esp[8];
            }

            if (cur->state == PROC_RUNNING) {
    /* Keep the Stage 0 shell at the highest priority.
     * The shell is the interactive foreground process. */
    if (cur->pid == 0) {
        cur->priority = 0;
        cur->quantum_ticks = quantum[0];
        cur->ticks_used = 0;
    } else {
        cur->ticks_used++;

        if (count_tick && cur->ticks_used >= cur->quantum_ticks) {
            if (cur->priority + 1 < MLFQ_LEVELS)
                cur->priority++;

            cur->ticks_used = 0;
            cur->quantum_ticks = quantum[cur->priority];
        }
    }
}
        }
    }

    /* Every PIT tick performs a scheduling decision; equal priority is RR. */
    rr_cursor = (cur_index + 1) % MAX_PROCESSES;
    next = pick_next(p, rr_cursor);
    if (!next && cur && runnable_at(cur, cur_index)) next = cur;
    if (!next) return (uint32_t)context_esp;

    if (cur && cur != next && cur->state == PROC_RUNNING)
        cur->state = PROC_READY;
    next->state = PROC_RUNNING;
    process_set_current(next);
    rr_cursor = ((uint32_t)(next - p) + 1) % MAX_PROCESSES;

    return next->esp;
}

uint32_t scheduler_tick(uint32_t *context_esp)
{
    if (!timer_started) return (uint32_t)context_esp;
    return dispatch(context_esp, true);
}

uint32_t scheduler_yield(uint32_t *context_esp)
{
    return dispatch(context_esp, false);
}

/* Clone the complete 4 KiB process stack and the interrupted register image.
 * The saved PUSHAD frame is relocated with the stack; EBP and any GPR pointing
 * inside the stack are adjusted by the same delta. Child EAX becomes 0 and the
 * parent receives the child's PID, matching fork() semantics. */
uint32_t scheduler_fork(uint32_t *context_esp)
{
    pcb_t *parent = process_current();
    pcb_t *child = (pcb_t *)0;
    pcb_t *p = process_table_mutable();
    uint32_t i;
    uint32_t delta;
    uint8_t *src;
    uint8_t *dst;
    uint32_t *child_ctx;

    if (!parent || !parent->has_stack) {
        if (context_esp) context_esp[7] = (uint32_t)-1;
        return (uint32_t)context_esp;
    }

    for (i = 1; i < MAX_PROCESSES; i++) {
        if (p[i].state == PROC_TERMINATED && !p[i].has_stack) {
            child = &p[i]; break;
        }
    }
    if (!child) {
        context_esp[7] = (uint32_t)-1;
        return (uint32_t)context_esp;
    }

    child->pid = 0;
    /* L11: allocate the child stack from the physical frame manager. */
    child->stack_base = pmm_alloc_frame();
    if (!child->stack_base) {
        context_esp[7] = (uint32_t)-1;
        return (uint32_t)context_esp;
    }
    child->stack_top = child->stack_base + STACK_SIZE;
    child->has_stack = 1;
    child->state = PROC_READY;
    child->entry = parent->entry;
    child->priority = parent->priority;
    child->quantum_ticks = parent->quantum_ticks;
    child->ticks_used = 0;
    child->wake_tick = 0;
    child->pid = process_allocate_pid();
    /* Give forked children a useful name while preserving the parent name. */
    for (i = 0; i < PROCESS_NAME_LEN - 1 && parent->name[i]; i++)
        child->name[i] = parent->name[i];
    child->name[i] = '\0';
    if (i < PROCESS_NAME_LEN - 1) {
        child->name[i++] = '-';
        child->name[i++] = 'c';
        child->name[i] = '\0';
    }

    src = (uint8_t *)parent->stack_base;
    dst = (uint8_t *)child->stack_base;
    for (i = 0; i < STACK_SIZE; i++) dst[i] = src[i];
    delta = child->stack_base - parent->stack_base;
    child_ctx = (uint32_t *)((uint32_t)context_esp + delta);

    /* PUSHAD GPR slots. */
    for (i = 0; i < 8; i++) {
        uint32_t v = child_ctx[i];
        if (v >= parent->stack_base && v < parent->stack_top)
            child_ctx[i] = v + delta;
    }
    child_ctx[7] = 0; /* fork() returns 0 in the child. */
    child->esp = (uint32_t)child_ctx;
    child->eip = child_ctx[8];
    /* Account for the new live PCB. */
    { extern void process_increment_count(void); process_increment_count(); }

    /* Parent return value. */
    context_esp[7] = child->pid;
    return (uint32_t)context_esp;
}

uint32_t scheduler_kill(uint32_t *context_esp, uint32_t pid)
{
    pcb_t *target = process_get(pid);
    pcb_t *cur = process_current();
    pcb_t *p = process_table_mutable();
    uint32_t target_index = 0;
    uint32_t i;

    if (!target || pid == 0) {
        context_esp[7] = (uint32_t)-1;
        return (uint32_t)context_esp;
    }

    /* Find PCB index. */
    for (i = 0; i < MAX_PROCESSES; i++) {
        if (&p[i] == target) {
            target_index = i;
            break;
        }
    }

    /* Remove blocked process from the sorted sleep queue. */
    sleep_queue_remove(target_index);

    if (target->has_stack) pmm_free_frame(target->stack_base);
    target->stack_base = 0;
    target->stack_top = 0;
    target->state = PROC_TERMINATED;
    target->esp = 0;
    target->eip = 0;
    target->wake_tick = 0;
    target->entry = (process_entry_t)0;
    target->has_stack = 0;

    process_decrement_count();
    context_esp[7] = 0;

    if (target != cur)
        return (uint32_t)context_esp;

    return dispatch(context_esp, false);
}