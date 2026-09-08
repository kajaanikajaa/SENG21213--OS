# SENG21213-OS — Stage 0: Kernel Foundations

> **Course**: SENG 21213 – Computer Architecture & Operating Systems  
> **Year**: 2nd Year, Software Engineering  
> **Assignment**: Build your own x86 Operating System

---

## What Is This?

This is **Stage 0** of your semester-long OS assignment. Over 5 lecture milestones
(Lectures 8–12), your team will transform this minimal kernel into a functioning
operating system with process management, threading, memory management, and a
file system.


seng21213-os/
├── boot/
│   └── boot.asm          ← MBR Bootloader (NASM, 16-bit → 32-bit transition)
├── kernel/
│   ├── kernel_entry.asm  ← Protected-mode entry, calls kernel_main()
│   ├── kernel.c          ← Main kernel: shell loop, command dispatch
│   ├── vga.c / vga.h     ← VGA 80×25 text-mode driver
│   ├── keyboard.c / .h   ← PS/2 keyboard polling driver
├── include/
│   └── types.h           ← Primitive types (no libc!)
├── linker.ld             ← Linker script (kernel at 0x10000)
├── Makefile              ← Build system
├── Dockerfile            ← Reproducible build environment
└── README.md             ← You are here


---

## Milestone Schedule

| Lecture | Milestone | Files to Add |
|---------|-----------|-------------|
| L08 | ✅ Stage 0 – Boot + VGA + Shell | *Given to you* |
| L09 | Process Management | `kernel/process.c`, `kernel/scheduler.c` |
| L10 | Threads & Synchronisation | `kernel/thread.c`, `kernel/mutex.c` |
| L11 | Memory Management | `kernel/pmm.c`, `kernel/vmm.c` |
| L12 | File System | `kernel/fs.c`, `kernel/ramdisk.c` |

---

## Quick Start

### Option A: Docker (Recommended for all platforms)

```bash
# 1. Install Docker Desktop (Windows/Mac) or Docker Engine (Linux)
# 2. Build the image once:
docker build -t seng21213-os-builder .

# 3. Build the OS:
docker run --rm -v "$(pwd)":/os seng21213-os-builder

# 4. Run in QEMU (install QEMU locally):
qemu-system-i386 -drive format=raw,file=seng21213-os.img -m 32M
```

### Option B: Native Linux/WSL2

```bash
# Ubuntu/Debian
sudo apt install nasm gcc gcc-multilib binutils qemu-system-x86 make

# Build
make all

# Run
make run
```

### Option C: macOS (Homebrew)

```bash
brew install nasm x86_64-elf-binutils qemu

# You also need an i686-elf-gcc cross-compiler:
# See: https://wiki.osdev.org/GCC_Cross-Compiler
make all
make run
```

---

## Understanding the Boot Process

```
Power On
  │
  ▼
BIOS (firmware in ROM)
  │  Loads 512-byte MBR from disk sector 1 into RAM at 0x7C00
  ▼
boot/boot.asm  (Real Mode, 16-bit)
  │  Prints "Loading SENG21213-OS..."
  │  Reads 64 sectors (kernel) from disk into RAM at 0x10000
  │  Sets up GDT (Global Descriptor Table)
  │  Switches CPU to 32-bit Protected Mode
  │  Far-jumps to 0x10000
  ▼
kernel/kernel_entry.asm  (Protected Mode, 32-bit)
  │  Calls kernel_main()
  ▼
kernel/kernel.c  →  kernel_main()
  │  vga_init()     – set up text display
  │  kb_init()      – set up keyboard
  │  print_splash() – welcome screen
  │  shell_run()    – interactive shell (infinite loop)
  ▼
Your code from here...
```

---

## Building Lecture 9: Process Management

When you reach Lecture 9, you'll add process support. Here's the interface to implement:

```c
/* kernel/process.h  — you write this! */

#define MAX_PROCESSES    16
#define STACK_SIZE     4096

typedef enum { READY, RUNNING, BLOCKED, TERMINATED } proc_state_t;

typedef struct pcb {
    uint32_t      pid;
    proc_state_t  state;
    uint32_t      esp;          /* Saved stack pointer */
    uint32_t      eip;          /* Saved instruction pointer */
    uint32_t      stack[STACK_SIZE / 4];
    struct pcb   *next;         /* For linked-list ready queue */
} pcb_t;

void   process_init(void);
pcb_t *process_create(void (*entry)(void));
void   process_yield(void);        /* Trigger context switch */
void   process_exit(void);
void   scheduler_tick(void);       /* Called by timer IRQ (Lecture 10) */
```

---

## Debugging Tips

```bash
# Debug with GDB
make run-debug
# In another terminal:
gdb
(gdb) target remote :1234
(gdb) set architecture i386
(gdb) symbol-file build/kernel.elf
(gdb) break kernel_main
(gdb) continue

# Inspect the disk image
xxd seng21213-os.img | head -32    # View MBR
xxd seng21213-os.img | grep -c aa55  # Verify boot signature
```

---

## Key Learning Resources

| Topic | Reference |
|-------|-----------|
| x86 Protected Mode | Intel IA-32 Manual, Vol 3, Chapter 3 |
| VGA Text Mode | OSDev Wiki: Text UI |
| Interrupts / IDT | Stallings Ch.1; OSDev: IDT |
| Process Management | Stallings Ch.3–4 (your lecture notes) |
| Memory Management | Stallings Ch.7–8 (your lecture notes) |
| OSDev community | https://wiki.osdev.org |

---

## Assessment Rubric (per milestone)

| Criterion | Weight |
|-----------|--------|
| Code compiles and kernel boots in QEMU | 30% |
| Feature implementation (correct behaviour) | 40% |
| Code quality and comments | 20% |
| Lab demo and viva questions | 10% |

---

*Happy hacking! Remember: every commercial OS started exactly like this.*


### Stage 0 Commands

The shell provides the following commands:

| Command | Description |
|---|---|
| `help` | List all available commands with descriptions |
| `clear` | Clear the VGA screen and reset the cursor |
| `echo <text>` | Print the supplied text |
| `version` | Display the kernel name and version |
| `colour <fg> <bg>` | Change foreground and background colours (0-15) |
| `halt` | Disable interrupts and halt the CPU |
| `mem` | Display the Stage 0 memory-map stub |

### Stage 0 Extensions

#### 1. Full ISO Keyboard Layout
Supports Shift, CapsLock, Ctrl, Alt and AltGr mappings.

**Test:**
- Type uppercase letters using Shift.
- Toggle CapsLock and type letters.
- Test AltGr combinations such as `@`, `#`, `{`, `}`, `~` and `|`.

#### 2. VGA Scrolling and Scrollback
The VGA driver maintains a scrollback buffer and supports Page-Up/Page-Down navigation.

**Test:**
1. Run the kernel with `make run`.
2. Enter many lines using `echo`.
3. Press Page-Up to view previous output.
4. Press Page-Down to return toward the latest output.
5. Type a new command and verify that the display returns to the latest output.

#### 3. ANSI Escape-Code Colour Support
The VGA driver supports ANSI SGR colour sequences for foreground and background colours.

**Test:**
- Use ANSI colour sequences through kernel output.
- Verify that supported foreground/background colour codes change the displayed text colour.

Supported ranges include:
- `0` - reset attributes
- `30-37` - standard foreground colours
- `40-47` - standard background colours
- `90-97` - bright foreground colours
- `100-107` - bright background colours

#### 4. Command History
The shell maintains a command history ring buffer and supports Up/Down arrow navigation.

**Test:**
1. Enter several commands.
2. Press Up to retrieve the previous command.
3. Press Up repeatedly to move further back.
4. Press Down to move forward through history.
5. Verify that consecutive duplicate commands are not stored repeatedly.

#### 5. GRUB2 Multiboot2 Support
The kernel contains a Multiboot2 header and can be loaded by GRUB2.

**Test:**
```bash
make grub
make grub-run

```

## Stage 1 – Process Management & Scheduling

Stage 1 extends the Stage 0 kernel with process management and CPU scheduling.

### Stage 1 Features

- **Process Control Block (PCB)**
  - PID
  - Process state
  - Saved stack pointer (`ESP`)
  - Entry point (`EIP`)
  - Process name
  - Scheduling priority

- **Process Creation**
  - `create_process(entry_fn)`
  - Each process receives a dedicated 4 KB stack.

- **PIT Timer**
  - Intel 8253/8254 PIT configured at **100 Hz**.
  - Each timer tick represents approximately **10 ms**.
  - IRQ0 drives the scheduler.

- **Context Switching**
  - Implemented in `kernel/switch.asm`.
  - Uses `PUSHAD` / `POPAD` to save and restore CPU registers.

- **Round-Robin Scheduling**
  - Runnable processes are scheduled using Round-Robin scheduling.

- **MLFQ Scheduling**
  - Three priority levels are supported.
  - Processes receive different time quanta based on priority.

- **Process Sleep**
  - `sleep(ms)` blocks a process for the requested duration.
  - Sleeping processes are maintained in a **sorted sleep queue** based on wake-up time.

- **Process Monitoring**
  - `ps` displays:
    - PID
    - State
    - Priority
    - Process name

- **Process Forking**
  - `fork()` creates a child process by duplicating the parent's process context and stack.

- **Process Termination**
  - `kill <pid>` terminates a process.
  - Terminated processes are removed from scheduling.

### Stage 1 Shell Commands

| Command | Description |
|---|---|
| `ps` | Display PID, state, priority and process name |
| `fork` | Create a fork test process |
| `kill <pid>` | Terminate a process |

### Stage 1 Demo Processes

Two concurrent demonstration processes are included:

| Process | Sleep Interval |
|---|---:|
| `demo-A` | 500 ms |
| `demo-B` | 1000 ms |

These demonstrate concurrent process execution and periodic sleep/wakeup.

### Stage 1 Testing

Build and run:

```bash
make clean
make
make run
```

## Stage 2 – Threads & Synchronisation

Stage 2 adds Lecture 10 concurrency primitives while retaining the Stage 0
shell and Stage 1 process/scheduler APIs.


### Stage 2 core

- `thread_create(fn, arg)` creates a kernel thread with a private execution stack.
- Threads retain a parent PCB reference and share the kernel address space.
- `mutex_t` provides blocking mutual exclusion.
- `semaphore_t` provides blocking counting synchronization.
- `racetest` demonstrates the `myglobal` lost-update race and the mutex-protected result.
- `pctest` demonstrates a size-5 bounded buffer using `empty`, `full`, and buffer-lock semaphores.

### Stage 2 extensions

- `pitest` demonstrates mutex priority inheritance.
- `rwtest` demonstrates a reader-writer lock with concurrent readers and exclusive writers.
- `deadtest` demonstrates resource-allocation/wait-for graph cycle detection using DFS.

### Stage 2 shell tests

```text
thtest
racetest
pctest
pitest
rwtest
deadtest
```

