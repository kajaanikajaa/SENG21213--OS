#ifndef PROCESS_H
#define PROCESS_H

#include "../include/types.h"

#define MAX_PROCESSES 16
#define STACK_SIZE 4096
#define PROCESS_NAME_LEN 16
#define PROCESS_STACK_POOL 0x00200000u

typedef enum {
    PROC_READY = 0,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_TERMINATED
} proc_state_t;

typedef void (*process_entry_t)(void);

typedef struct pcb {
    uint32_t pid;
    proc_state_t state;
    uint32_t esp;
    uint32_t eip;
    uint32_t stack_base;
    uint32_t stack_top;
    uint32_t wake_tick;
    uint8_t priority;
    uint8_t quantum_ticks;
    uint8_t ticks_used;
    uint8_t has_stack;
    process_entry_t entry;
    char name[PROCESS_NAME_LEN];
} pcb_t;

void process_init(void);
pcb_t *create_process(process_entry_t entry);
pcb_t *process_create_named(process_entry_t entry, const char *name);
void process_exit(void);
void sleep(uint32_t ms);
int fork(void);
int kill(uint32_t pid);

pcb_t *process_current(void);
pcb_t *process_get(uint32_t pid);
const pcb_t *process_table(void);
uint32_t process_count(void);
const char *process_state_name(proc_state_t state);


#endif
