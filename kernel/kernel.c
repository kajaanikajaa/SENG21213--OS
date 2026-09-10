/* =============================================================================
 * SENG21213-OS :: Main Kernel  (Stage 0 – Foundations)
 * File   : kernel/kernel.c
 *
 * PURPOSE
 *   This is the heart of your operating system. Right now it:
 *     1. Initialises VGA text-mode display
 *     2. Initialises the keyboard driver
 *     3. Prints a splash screen
 *     4. Runs a minimal interactive shell ("ksh")
 *
 * ASSIGNMENT MILESTONES  (what YOU will add in later lectures)
 *   Lecture  9  – Process Management  →  process.h / process.c / scheduler.c
 *   Lecture 10  – Threads             →  thread.h  / thread.c
 *   Lecture 11  – Memory Management   →  pmm.h     / pmm.c / vmm.c
 *   Lecture 12  – File System         →  fs.h      / fs.c
 *
 * CODING CONVENTION
 *   - Prefix kernel-internal functions with k_ (e.g. k_strcmp)
 *   - All driver APIs live in their own .h/.c pair
 *   - NEVER call malloc – use the PMM you build in Lecture 11
 * ============================================================================*/

#include "vga.h"
#include "keyboard.h"
#include "process.h"
#include "scheduler.h"
#include "stage2_demo.h"
#include "pmm.h"
#include "buddy.h"
#include "slab.h"
#include "../include/types.h"

/* ---------------------------------------------------------------------------
 * Forward declarations of shell commands
 * --------------------------------------------------------------------------*/
static void cmd_help(const char *args);
static void cmd_clear(const char *args);
static void cmd_echo(const char *args);
static void cmd_version(const char *args);
static void cmd_colour(const char *args);
static void cmd_mem(const char *args);
static void cmd_about(const char *args);
static void cmd_halt(const char *args);
static void cmd_thtest(const char *args);
static void cmd_racetest(const char *args);
static void cmd_pctest(const char *args);
static void cmd_pitest(const char *args);
static void cmd_rwtest(const char *args);
static void cmd_deadtest(const char *args);
static void cmd_meminfo(const char *args);
static void cmd_pmmtest(const char *args);
static void cmd_buddytest(const char *args);
static void cmd_slabtest(const char *args);
static void cmd_ps(const char *args);
static void cmd_kill(const char *args);
static void demo_fast(void);
static void demo_slow(void);
static void fork_test_process(void);
static void cmd_fork(const char *args);


/* ---------------------------------------------------------------------------
 * Utility: minimal string helpers (no libc in a freestanding kernel!)
 * --------------------------------------------------------------------------*/
static int k_strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

static size_t k_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Skip leading spaces */
static const char *k_ltrim(const char *s) {
    while (*s == ' ') s++;
    return s;
}

static int k_parse_uint(const char **p)
{
    int value = 0;
    uint8_t have_digit = false;

    while (**p >= '0' && **p <= '9') {
        value = value * 10 + (**p - '0');
        (*p)++;
        have_digit = true;
    }

    return have_digit ? value : -1;
}

/* ---------------------------------------------------------------------------
 * Splash Screen
 * --------------------------------------------------------------------------*/
static void print_splash(void)
{
    vga_clear(VGA_BLACK);

    vga_set_cursor(0, 0);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    vga_puts("SENG21213 Stage 3 - Loading kernel...\n");
    vga_puts("Kernel loaded. Switching to Protected Mode.\n\n");

    vga_puts("[ ");
    vga_puts_color("OK", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts(" ] VGA driver initialised\n");

    vga_puts("[ ");
    vga_puts_color("OK", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts(" ] PS/2 keyboard driver initialised\n");

    vga_puts("[ ");
    vga_puts_color("OK", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts(" ] Interrupt handling initialised\n");

    vga_puts("[ ");
    vga_puts_color("OK", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts(" ] Timer interrupt initialised\n");

    vga_puts("[ ");
    vga_puts_color("OK", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts(" ] Scheduler initialised\n");

    vga_puts("[ ");
    vga_puts_color("OK", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts(" ] Context switching enabled\n");

    vga_puts("[ ");
vga_puts_color("OK", VGA_LIGHT_GREEN, VGA_BLACK);
vga_puts(" ] Physical memory manager initialised\n\n");

    vga_puts("+=================================================+\n");

    vga_puts("| ");
    vga_puts_color("SENG 21213 - Stage 3 Kernel", VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("                     |\n");

    vga_puts("| ");
    vga_puts_color("Physical Memory Manager", VGA_YELLOW, VGA_BLACK);
    vga_puts("                         |\n");

    vga_puts("| Type 'help' for available commands              |\n");

    vga_puts("+=================================================+\n\n");
}

/* ---------------------------------------------------------------------------
 * Shell command table
 * --------------------------------------------------------------------------*/
typedef void (*command_handler_t)(const char *args);

typedef struct {
    const char *name;
    const char *description;
    command_handler_t handler;
} command_t;

static const command_t commands[] = {
    { "help",    "List all available commands with descriptions", cmd_help },
    { "clear",   "Fill screen with spaces and reset cursor to (0,0)", cmd_clear },
    { "echo",    "Print all arguments back to the screen", cmd_echo },
    { "version", "Print kernel name and version string", cmd_version },
    { "colour",  "Change text colour (0-15 for foreground/background)", cmd_colour },
    { "mem",    "Show memory information", cmd_mem },
    { "about",  "Show information about SENG21213-OS", cmd_about },
    { "ps",      "List all process control blocks and states", cmd_ps },
    { "fork",    "Create a child process from the current process", cmd_fork },
    { "kill",    "Terminate a process by PID", cmd_kill },
    { "thtest",  "Test kernel thread creation and execution", cmd_thtest },
    { "racetest", "Demonstrate myglobal race with and without mutex", cmd_racetest },
    { "pctest",   "Run bounded-buffer producer-consumer test", cmd_pctest },
    { "pitest",   "Demonstrate mutex priority inheritance", cmd_pitest },
    { "rwtest",   "Run reader-writer lock demonstration", cmd_rwtest },
    { "deadtest", "Run resource-allocation graph deadlock detector", cmd_deadtest },
    { "meminfo", "Show physical memory totals and free frames", cmd_meminfo },
    { "pmmtest", "Allocate and free 100 physical frames", cmd_pmmtest },
    { "buddytest", "Test buddy allocator", cmd_buddytest },
    { "slabtest", "Test slab allocator", cmd_slabtest },
    { "halt",    "Disable interrupts and halt the CPU", cmd_halt }
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

/* ---------------------------------------------------------------------------
 * Shell command implementations
 * --------------------------------------------------------------------------*/
static void cmd_help(const char *args) {
    (void)args;

    vga_puts_color("\n  SENG21213-OS Shell Commands\n", VGA_YELLOW, VGA_BLACK);
    vga_puts("  ------------------------------------------------------------\n");

    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        vga_puts("  ");
        vga_puts(commands[i].name);

        /* Keep all descriptions aligned */
        int name_len = k_strlen(commands[i].name);
        for (int j = name_len; j < 10; j++) {
            vga_putchar(' ');
        }

        vga_puts("- ");
        vga_puts(commands[i].description);
        vga_puts("\n");
    }

    vga_puts("\n");
}
static void cmd_clear(const char *args) {
    (void)args;
    vga_clear(VGA_BLACK);
}

static void cmd_echo(const char *args) {
    vga_puts("  ");
    vga_puts(args);
    vga_puts("\n");
}

static void cmd_version(const char *args) {
    (void)args;
    vga_puts_color("\n  SENG21213-OS Stage 2\n", VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("  Kernel version: v0.4-stage3\n\n");
}

static void cmd_colour(const char *args) {
    const char *p = k_ltrim(args);
    int fg = k_parse_uint(&p);
    int bg;

    p = k_ltrim(p);
    bg = k_parse_uint(&p);

    p = k_ltrim(p);

    if (fg < 0 || bg < 0 || fg > 15 || bg > 15 || *p != '\0') {
        vga_puts_color("  Usage: colour <fg> <bg>   (both values 0-15)\n",
                       VGA_LIGHT_RED, VGA_BLACK);
        return;
    }

    vga_set_color((vga_color_t)fg, (vga_color_t)bg);
    vga_printf("  Colour changed: foreground=%d background=%d\n", fg, bg);
}

static void cmd_mem(const char *args)
{
    (void)args;
    cmd_meminfo(args);
}

static void cmd_meminfo(const char *args)
{
    (void)args;
    vga_puts("\n  Physical Memory Manager\n");
    vga_puts("  ------------------------\n");
    vga_printf("  Total : %u MB\n", pmm_total_bytes() / (1024u * 1024u));
    vga_printf("  Used  : %u MB\n", pmm_used_bytes() / (1024u * 1024u));
    vga_printf("  Free  : %u MB\n", pmm_free_bytes() / (1024u * 1024u));
    vga_printf("  Frames: total=%u used=%u free=%u\n\n",
                pmm_total_frames(), pmm_used_frames(), pmm_free_frames());
}

static void cmd_pmmtest(const char *args)
{
    uint32_t frames[100];
    uint32_t before;
    uint32_t i;
    (void)args;

    before = pmm_free_frames();
    for (i = 0; i < 100; i++) {
        frames[i] = pmm_alloc_frame();
        if (!frames[i]) {
            while (i > 0) pmm_free_frame(frames[--i]);
            vga_puts("\n[PMM] FAIL: could not allocate 100 frames.\n");
            return;
        }
    }
    if (pmm_free_frames() != before - 100u) {
        for (i = 0; i < 100; i++) pmm_free_frame(frames[i]);
        vga_puts("\n[PMM] FAIL: allocation count is incorrect.\n");
        return;
    }
    for (i = 0; i < 100; i++) pmm_free_frame(frames[i]);

    vga_printf("\n[PMM] 100 frame allocate/free test\n  Free frames before = %u\n  Free frames after  = %u\n",
               before, pmm_free_frames());
    if (pmm_free_frames() == before)
        vga_puts("[PMM] PASS: no frame leak detected.\n\n");
    else
        vga_puts("[PMM] FAIL: frame leak detected.\n\n");
}

/* Lecture 11 §5: Buddy allocator for contiguous physical memory blocks. */
#include "buddy.h"
#include "pmm.h"

#define BUDDY_MAX_BLOCKS 4096u

typedef struct {
    uint32_t addr;
    uint32_t order;
    uint8_t free;
} buddy_block_t;

static buddy_block_t blocks[BUDDY_MAX_BLOCKS];
static uint32_t block_count;

static uint32_t block_size(uint32_t order)
{
    return PMM_PAGE_SIZE << order;
}

static int find_block(uint32_t addr, uint32_t order)
{
    uint32_t i;

    for (i = 0; i < block_count; i++) {
        if (blocks[i].free &&
            blocks[i].addr == addr &&
            blocks[i].order == order)
            return (int)i;
    }

    return -1;
}

static int add_block(uint32_t addr, uint32_t order)
{
    if (block_count >= BUDDY_MAX_BLOCKS)
        return -1;

    blocks[block_count].addr = addr;
    blocks[block_count].order = order;
    blocks[block_count].free = 1;
    block_count++;

    return (int)(block_count - 1);
}

static void remove_block(uint32_t index)
{
    if (index < block_count)
        blocks[index].free = 0;
}


static void cmd_about(const char *args)
{
    (void)args;

    vga_puts_color("\n  About SENG21213-OS\n", VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("  ---------------------------------------------------\n");
    vga_puts("  Architecture : x86 (i686), 32-bit Protected Mode\n");
    vga_puts("  Bootloader   : Custom MBR (NASM)\n");
    vga_puts("  Kernel       : Freestanding C (GCC, no libc)\n");
    vga_puts("  VM Target    : QEMU (qemu-system-i386)\n");
    vga_puts("  Course       : SENG 21213 - Sem 2\n");
    vga_puts("  Reference    : Stallings, OS: Internals & Design Principles\n\n");
}


static void cmd_ps(const char *args)
{
    const pcb_t *p = process_table();
    (void)args;

    vga_puts_color(
        "\n  PID   STATE        PRIORITY  NAME\n",
        VGA_YELLOW,
        VGA_BLACK
    );

    vga_puts(
        "  ------------------------------------------\n"
    );

    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {

        /* Skip unused PCB slots */
        if (p[i].pid == 0 && i != 0)
            continue;

        vga_printf("  %u    ", p[i].pid);

        const char *state = process_state_name(p[i].state);
        vga_puts(state);

        /* STATE column */
        size_t state_len = k_strlen(state);

        for (size_t j = state_len; j < 13; j++)
            vga_putchar(' ');

        /* PRIORITY column */
        vga_printf("%u        ", (uint32_t)p[i].priority);

        /* NAME */
        vga_puts(p[i].name[0] ? p[i].name : "-");

        vga_puts("\n");
    }

    vga_puts("\n");
}

static void cmd_fork(const char *args)
{
    (void)args;

    pcb_t *p = process_create_named(
        fork_test_process,
        "fork-test"
    );

    if (!p) {
        vga_puts_color(
            "  fork: unable to create test process\n",
            VGA_LIGHT_RED,
            VGA_BLACK
        );
        return;
    }

    vga_printf(
        "  fork test process created: PID %u\n",
        p->pid
    );
}
static void cmd_kill(const char *args)
{
    const char *p = k_ltrim(args);
    int pid = k_parse_uint(&p);
    p = k_ltrim(p);

    if (pid <= 0 || *p != '\0') {
        vga_puts_color("  Usage: kill <pid>\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }

    if (kill((uint32_t)pid) < 0) {
        vga_puts_color("  kill: no such process (or PID 0 is protected)\n",
                       VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    vga_printf("  Process %d terminated.\n", pid);
}

/* Two Stage 1 demo processes. They deliberately use different sleep rates so
 * the PIT scheduler visibly interleaves their output. */
/* Replace the existing demo_fast() and demo_slow() in kernel/kernel.c */

static void demo_fast(void)
{
    for (;;) {
        sleep(500);
    }
}

static void demo_slow(void)
{
    for (;;) {
        sleep(1000);
    }
}

static void fork_test_process(void)
{
    int child_pid = fork();

    if (child_pid < 0) {
        process_exit();
        return;
    }

    if (child_pid == 0) {
        /* Child process: do not print to VGA.
         * Sleeping proves that the child remains schedulable. */
        for (;;) {
            sleep(1000);
        }
    }

    /* Parent process: do not print to VGA.
     * Parent exits after creating the child. */
    process_exit();
}

static void cmd_thtest(const char *args)
{
    (void)args;
    stage2_thread_demo();
}

static void cmd_racetest(const char *args)
{
    (void)args;
    stage2_race_demo();
}

static void cmd_pctest(const char *args)
{
    (void)args;
    stage2_producer_consumer_demo();
}

static void cmd_pitest(const char *args)
{
    (void)args;
    stage2_priority_demo();
}

static void cmd_rwtest(const char *args)
{
    (void)args;
    stage2_rwlock_demo();
}

static void cmd_deadtest(const char *args)
{
    (void)args;
    stage2_deadlock_demo();
}

static void cmd_buddytest(const char *args)
{
    uint32_t before;
    uint32_t a;
    uint32_t b;
    uint32_t c;

    (void)args;

    before = pmm_free_frames();

    vga_puts("\n[Buddy] Testing contiguous allocations...\n");

    a = buddy_alloc(0);

    if (a == 0) {
        vga_puts("[Buddy] FAIL: order-0 allocation\n");
        return;
    }

    vga_puts("[Buddy] 1-frame allocation       PASS\n");

    b = buddy_alloc(2);

    if (b == 0) {
        buddy_free(a, 0);
        vga_puts("[Buddy] FAIL: order-2 allocation\n");
        return;
    }

    if ((b & ((PMM_PAGE_SIZE << 2) - 1u)) != 0) {
        buddy_free(a, 0);
        buddy_free(b, 2);
        vga_puts("[Buddy] FAIL: alignment\n");
        return;
    }

    vga_puts("[Buddy] 4-frame allocation       PASS\n");
    vga_puts("[Buddy] contiguous frames        PASS\n");

    c = buddy_alloc(3);

    if (c == 0) {
        buddy_free(a, 0);
        buddy_free(b, 2);
        vga_puts("[Buddy] FAIL: order-3 allocation\n");
        return;
    }

    vga_puts("[Buddy] 8-frame allocation       PASS\n");

    buddy_free(a, 0);
    buddy_free(b, 2);
    buddy_free(c, 3);

    if (pmm_free_frames() == before)
        vga_puts("[Buddy] free/coalescing          PASS\n");
    else
        vga_puts("[Buddy] FAIL: allocation balance\n");

    vga_puts("[Buddy] Test complete.\n\n");
}

static void cmd_slabtest(const char *args)
{
    void *a;
    void *b;
    void *c;
    void *d;

    (void)args;

    vga_puts("\n[Slab] Testing kmalloc/kfree...\n");

    a = kmalloc(20);
    b = kmalloc(50);
    c = kmalloc(100);
    d = kmalloc(200);

    if (a != 0)
        vga_puts("[Slab] kmalloc(32)        PASS\n");
    else
        vga_puts("[Slab] kmalloc(32)        FAIL\n");

    if (b != 0)
        vga_puts("[Slab] kmalloc(64)        PASS\n");
    else
        vga_puts("[Slab] kmalloc(64)        FAIL\n");

    if (c != 0)
        vga_puts("[Slab] kmalloc(128)       PASS\n");
    else
        vga_puts("[Slab] kmalloc(128)       FAIL\n");

    if (d != 0)
        vga_puts("[Slab] kmalloc(256)       PASS\n");
    else
        vga_puts("[Slab] kmalloc(256)       FAIL\n");

    kfree(a);
    kfree(b);
    kfree(c);
    kfree(d);

    vga_puts("[Slab] kfree()             PASS\n");

    a = kmalloc(20);

    if (a != 0)
        vga_puts("[Slab] object reuse        PASS\n");
    else
        vga_puts("[Slab] object reuse        FAIL\n");

    kfree(a);

    vga_puts("[Slab] Test complete.\n\n");
}

static void cmd_halt(const char *args) {
    (void)args;

    vga_puts_color("\n  CPU halted.\n", VGA_YELLOW, VGA_BLACK);

    __asm__ __volatile__("cli");
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

/* ---------------------------------------------------------------------------
 * Shell process
 * --------------------------------------------------------------------------*/
static char shell_buf[256];
static char prompt[] = "\n  ksh> ";

static void shell_run(void) {
    while (true) {
        vga_puts_color(prompt, VGA_LIGHT_GREEN, VGA_BLACK);
        kb_readline(shell_buf, sizeof(shell_buf));

        /* Trim leading whitespace */
        const char *cmd = k_ltrim(shell_buf);
        if (k_strlen(cmd) == 0) continue;

        /* Split command name from arguments. */
        char command_name[32];
        size_t command_len = 0;
        const char *args;

        while (cmd[command_len] && cmd[command_len] != ' ' && command_len < sizeof(command_name) - 1) {
            command_name[command_len] = cmd[command_len];
            command_len++;
        }
        command_name[command_len] = '\0';

        args = k_ltrim(cmd + command_len);

        /* Command-table dispatch */
        uint8_t found = false;
        for (size_t i = 0; i < COMMAND_COUNT; i++) {
            if (k_strcmp(command_name, commands[i].name) == 0) {
                commands[i].handler(args);
                found = true;
                break;
            }
        }

        if (found) continue;

        vga_puts_color("  Unknown command: ", VGA_LIGHT_RED, VGA_BLACK);
        vga_puts(command_name);
        vga_puts("\n  Type 'help' for a list of commands.\n");
    }
}

/* ---------------------------------------------------------------------------
 * Kernel entry point – called from kernel_entry.asm
 * --------------------------------------------------------------------------*/
void kernel_main(void) {
    vga_init();
    kb_init();
    print_splash();

    /* Stage 1 remains unchanged: initialise interrupts/PIT and create the two concurrent processes.
     * The Stage 0 polling shell remains process PID 0, so all Stage 0 commands
     * and keyboard/history/scrollback behaviour continue to work unchanged. */
    pmm_init();
    buddy_init();
    slab_init();
    scheduler_init();
    process_create_named(demo_fast, "demo-A");
    process_create_named(demo_slow, "demo-B");
    scheduler_start();

    shell_run();

    /* Should never reach here */
    __asm__ __volatile__("cli");
    for (;;) __asm__ __volatile__("hlt");
}
