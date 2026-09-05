# =============================================================================
# SENG21213-OS :: Makefile
# =============================================================================
#
# TOOLCHAIN SETUP
#   Option A (recommended): Use the Docker environment provided
#              docker build -t seng21213-os .
#              docker run --rm -v $(pwd):/os seng21213-os
#
#   Option B: Native cross-compiler
#              Install i686-elf-gcc (see README.md § Toolchain)
#              Set CC and LD to point to the cross tools.
#
#   Option C: Ubuntu/Debian with gcc-multilib
#              sudo apt install gcc gcc-multilib nasm qemu-system-i386 grub-common grub-pc-bin xorriso
#
# =============================================================================

AS       := nasm
ASFLAGS  := -f elf32

ifneq (, $(shell which i686-elf-gcc 2>/dev/null))
    CC := i686-elf-gcc
    LD := i686-elf-ld
else
    CC := gcc
    LD := ld
endif

CFLAGS := -m32 -ffreestanding -fno-stack-protector -fno-pie -nostdlib \
          -Wall -Wextra -O2 -I./include
LDFLAGS := -m elf_i386 -nostdlib

BOOT_SRC := boot/boot.asm
BOOT_BIN := build/boot.bin
KERNEL_ASM_SRC := kernel/kernel_entry.asm
KERNEL_ASM_OBJ := build/kernel_entry.o
KERNEL_C_SRCS := kernel/kernel.c kernel/vga.c kernel/keyboard.c
KERNEL_C_OBJS := $(patsubst kernel/%.c,build/%.o,$(KERNEL_C_SRCS))
KERNEL_ELF := build/kernel.elf
KERNEL_BIN := build/kernel.bin
OS_IMAGE := seng21213-os.img
GRUB_DIR := build/grub
GRUB_ISO := seng21213-os-grub.iso

.PHONY: all clean run grub grub-run info

all: $(OS_IMAGE)
	@echo ""
	@echo "  Build successful -> $(OS_IMAGE)"
	@echo "  Run with: make run"
	@echo "  GRUB2 image: make grub"
	@echo ""

$(BOOT_BIN): $(BOOT_SRC)
	@mkdir -p build
	@echo "  [AS]  $<"
	$(AS) -f bin $< -o $@

$(KERNEL_ASM_OBJ): $(KERNEL_ASM_SRC)
	@mkdir -p build
	@echo "  [AS]  $<"
	$(AS) $(ASFLAGS) $< -o $@

build/%.o: kernel/%.c
	@mkdir -p build
	@echo "  [CC]  $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_ELF): $(KERNEL_ASM_OBJ) $(KERNEL_C_OBJS)
	@echo "  [LD]  $@"
	$(LD) $(LDFLAGS) -T linker.ld $^ -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	@echo "  [OBJCOPY] $@"
	objcopy -O binary $< $@

$(OS_IMAGE): $(BOOT_BIN) $(KERNEL_BIN)
	@echo "  [IMG]  Creating $(OS_IMAGE)..."
	dd if=/dev/zero bs=512 count=2880 of=$(OS_IMAGE) 2>/dev/null
	dd if=$(BOOT_BIN) conv=notrunc bs=512 count=1 of=$(OS_IMAGE) 2>/dev/null
	dd if=$(KERNEL_BIN) conv=notrunc bs=512 seek=1 of=$(OS_IMAGE) 2>/dev/null
	@echo "  [IMG]  $(OS_IMAGE) ready"

$(GRUB_ISO): $(KERNEL_ELF)
	@rm -rf $(GRUB_DIR)
	@mkdir -p $(GRUB_DIR)/boot/grub
	@cp $(KERNEL_ELF) $(GRUB_DIR)/boot/kernel.elf
	@printf 'set timeout=0\nset default=0\nmenuentry "SENG21213-OS Stage 0" {\n    multiboot2 /boot/kernel.elf\n    boot\n}\n' > $(GRUB_DIR)/boot/grub/grub.cfg
	@echo "  [GRUB] Creating $(GRUB_ISO)..."
	grub-mkrescue -o $(GRUB_ISO) $(GRUB_DIR)

run: $(OS_IMAGE)
	@echo "  Starting QEMU..."
	qemu-system-i386 -drive format=raw,file=$(OS_IMAGE) -m 32M

grub: $(GRUB_ISO)
	@echo "  GRUB2 image ready -> $(GRUB_ISO)"

grub-run: $(GRUB_ISO)
	qemu-system-i386 -cdrom $(GRUB_ISO) -m 32M

info: $(KERNEL_ELF)
	@echo "Toolchain: CC=$(CC) AS=$(AS) LD=$(LD)"
	@objdump -h $(KERNEL_ELF)

clean:
	rm -rf build $(OS_IMAGE) $(GRUB_ISO)
	@echo "  Cleaned."
