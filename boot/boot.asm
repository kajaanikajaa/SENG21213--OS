; =============================================================================
; SENG21213-OS :: Stage 3 bootloader
; BIOS E820 map is saved at 0x8000 before entering protected mode.
; =============================================================================
[BITS 16]
[ORG 0x7C00]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl

    ; Save the BIOS E820 memory map at 0x8000.
    mov word [0x8000], 0
    xor ebx, ebx
    mov di, 0x8004
.e820:
    cmp word [0x8000], 128
    jae .e820_done
    xor ax, ax
    mov es, ax
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done
    add di, 24
    inc word [0x8000]
    test ebx, ebx
    jnz .e820
.e820_done:

    ; Load the kernel at physical address 0x10000.
    mov ax, 0x1000
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, 64
    xor ch, ch
    mov cl, 2
    xor dh, dh
    mov dl, [boot_drive]
    int 0x13
    jc real_mode_halt

    ; Enter 32-bit protected mode.
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:init_pm32

[BITS 32]
init_pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    call 0x10000
protected_halt:
    cli
    hlt
    jmp protected_halt

[BITS 16]
real_mode_halt:
    cli
    hlt
    jmp real_mode_halt

boot_drive db 0

gdt_start:
    dq 0
    dw 0xFFFF
    dw 0
    db 0
    db 10011010b
    db 11001111b
    db 0
    dw 0xFFFF
    dw 0
    db 0
    db 10010010b
    db 11001111b
    db 0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 510 - ($ - $$) db 0
dw 0xAA55
