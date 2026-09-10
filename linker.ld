/* =============================================================================
 * SENG21213-OS :: Linker Script
 * File   : linker.ld
 *
 * Places the kernel binary starting at physical address 0x10000.
 * The bootloader loads the kernel to 0x1000:0x0000 = physical 0x10000.
 * kernel_entry.asm is the very first object, so _start is at 0x10000.
 * ============================================================================*/

OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text : ALIGN(16)
    {
        *(.text.start)
        KEEP(*(.multiboot2))
        *(.text*)
    }

    .rodata : ALIGN(16)
    {
        *(.rodata*)
    }

    .data : ALIGN(16)
    {
        *(.data*)
    }

    .bss : ALIGN(16)
    {
        *(COMMON)
        *(.bss*)
    }

    _kernel_end = .;

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.comment)
        *(.note*)
    }
}
