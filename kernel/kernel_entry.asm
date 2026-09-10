; =============================================================================
; SENG21213-OS :: Kernel Entry Point
; File   : kernel/kernel_entry.asm
; Purpose: Bridges the bootloader (NASM) to the C kernel. Sets up calling
;          conventions then calls kernel_main().
; =============================================================================

[BITS 32]
[EXTERN kernel_main]
[GLOBAL _start]

; Multiboot2 header for the GRUB2 Stage 0 extension.
section .text.start
_start:
    mov esp, stack_top
    xor ebp, ebp
    call kernel_main

    cli
.halt:
    hlt
    jmp .halt

section .multiboot2
align 8
mb2_header:
    dd 0xE85250D6
    dd 0
    dd mb2_header_end - mb2_header
    dd -(0xE85250D6 + 0 + (mb2_header_end - mb2_header))
    dw 0
    dw 0
    dd 8
mb2_header_end:

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:
