; SENG21213-OS :: Interrupt/context switch entry points
; The stack layout at entry to the C scheduler is:
;   [EDI..EAX] from PUSHAD, followed by [EIP, CS, EFLAGS] from the CPU.

[BITS 32]

[GLOBAL irq0_entry]
[GLOBAL scheduler_yield_entry]
[GLOBAL scheduler_fork_entry]
[GLOBAL scheduler_kill_entry]
[EXTERN scheduler_tick]
[EXTERN scheduler_yield]
[EXTERN scheduler_fork]
[EXTERN scheduler_kill]

irq0_entry:
    pushad
    push esp
    call scheduler_tick
    add esp, 4
    mov edx, eax
    mov al, 0x20
    out 0x20, al          ; PIC EOI for IRQ0
    mov esp, edx
    popad
    iretd

scheduler_yield_entry:
    pushad
    push esp
    call scheduler_yield
    add esp, 4
    mov esp, eax
    popad
    iretd

scheduler_fork_entry:
    pushad
    push esp
    call scheduler_fork
    add esp, 4
    mov esp, eax
    popad
    iretd

scheduler_kill_entry:
    pushad
    mov eax, [esp + 28]   ; saved EAX = kill(pid) argument
    mov edx, esp            ; preserve PUSHAD-frame pointer
    push eax
    push edx
    call scheduler_kill
    add esp, 8
    mov esp, eax
    popad
    iretd

