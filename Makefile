# NEXUS-OS — build Phase 1 (bootloader Multiboot2 + Coordinator + découverte ACPI/MADT)
CC   := gcc
LD   := ld
ASM  := nasm

BUILD  := build
ISODIR := $(BUILD)/iso
KERNEL := $(BUILD)/nexus.elf
ISO    := $(BUILD)/nexus-os.iso

SMP ?= 4
MEM ?= 512

# Nom de l'OS : source unique = branding.h (extrait pour le titre GRUB / logs build).
OS_NAME := $(shell sed -n 's/^\#define OS_NAME[[:space:]]*"\(.*\)".*/\1/p' branding.h)

INCLUDES := -I. -ILIBS/libkc/include -ILIBS/librt/include -IHAL/serial -IHAL/acpi \
            -IBOOT/stage2 -ICOORDINATOR/topology -ICOORDINATOR/mm -ICOORDINATOR/smp \
            -IIPC/proto -IIPC/ring -IIPC/doorbell

# Freestanding, sans pile rouge, sans SSE/MMX/x87 (CR4.OSFXSR non configuré),
# modèle mémoire "small" (noyau en < 2 GiB), non-PIE.
CFLAGS := -ffreestanding -nostdlib -fno-stack-protector -fno-pic -fno-pie \
          -mno-red-zone -mgeneral-regs-only -mcmodel=small \
          -std=gnu11 -O2 -Wall -Wextra \
          -fno-builtin -fno-tree-loop-distribute-patterns \
          -fno-asynchronous-unwind-tables $(INCLUDES)

LDFLAGS  := -n -z max-page-size=0x1000 -T TOOLS/image/linker.ld
ASMFLAGS := -f elf64

C_SRC := \
    LIBS/libkc/string.c \
    LIBS/libkc/printf.c \
    HAL/serial/serial.c \
    HAL/acpi/acpi.c \
    BOOT/stage2/multiboot2.c \
    COORDINATOR/topology/topology.c \
    COORDINATOR/mm/mm.c \
    COORDINATOR/smp/smp.c \
    COORDINATOR/smp/tramp_blob.c \
    IPC/ring/ring.c \
    IPC/doorbell/doorbell.c \
    COORDINATOR/core/main.c

ASM_SRC := BOOT/stage2/boot.asm COORDINATOR/smp/isr.asm

# Trampoline AP : blob binaire à plat (org 0x8000), incorporé par tramp_blob.c.
TRAMP_BIN := $(BUILD)/trampoline.bin

C_OBJ   := $(patsubst %.c,$(BUILD)/%.o,$(C_SRC))
ASM_OBJ := $(patsubst %.asm,$(BUILD)/%.o,$(ASM_SRC))
OBJ     := $(ASM_OBJ) $(C_OBJ)

QEMU_FLAGS := -smp $(SMP) -m $(MEM) -serial stdio -no-reboot \
              -device isa-debug-exit,iobase=0xf4,iosize=0x04

.PHONY: all iso run run-gui test clean

all: $(KERNEL)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(ASM) $(ASMFLAGS) $< -o $@

# Blob trampoline (flat binary, org 0x8000)
$(TRAMP_BIN): COORDINATOR/smp/trampoline.asm
	@mkdir -p $(dir $@)
	$(ASM) -f bin $< -o $@

# tramp_blob.c fait un .incbin de build/trampoline.bin -> dépendance explicite
$(BUILD)/COORDINATOR/smp/tramp_blob.o: $(TRAMP_BIN)

$(KERNEL): $(OBJ) TOOLS/image/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJ)
	@echo "==> linked $@"

iso: $(ISO)

$(ISO): $(KERNEL) TOOLS/image/grub.cfg.in branding.h
	@rm -rf $(ISODIR)
	@mkdir -p $(ISODIR)/boot/grub
	@cp $(KERNEL) $(ISODIR)/boot/nexus.elf
	@sed 's/@OS_NAME@/$(OS_NAME)/g' TOOLS/image/grub.cfg.in > $(ISODIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISODIR) 2>/dev/null
	@echo "==> built $(ISO)  (OS_NAME=$(OS_NAME))"

# Boot interactif (halt en fin de Phase 1 ; Ctrl-A X pour quitter QEMU).
run: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) $(QEMU_FLAGS) -display none

run-gui: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) $(QEMU_FLAGS)

# Validation automatisée (unitaire hôte + intégration QEMU Phase 3).
test: test-unit test-phase3

test-phase1: $(ISO)
	@SMP=$(SMP) MEM=$(MEM) ISO=$(ISO) bash TESTS/qemu/run_phase1.sh

test-phase2: $(ISO)
	@SMP=$(SMP) MEM=$(MEM) ISO=$(ISO) bash TESTS/qemu/run_phase2.sh

test-phase3: $(ISO)
	@SMP=$(SMP) MEM=$(MEM) ISO=$(ISO) bash TESTS/qemu/run_phase3.sh

# Matrice de tests poussés (cœurs × mémoire + stress).
test-matrix: $(ISO)
	@bash TESTS/qemu/matrix.sh

# Test unitaire hôte du ring SPSC (pthreads, 10^7 messages).
test-unit:
	@$(CC) -O2 -pthread -ILIBS/libkc/include -IIPC/proto -IIPC/ring \
	    TESTS/unit/test_ring.c IPC/ring/ring.c -o $(BUILD)/test_ring
	@$(BUILD)/test_ring

clean:
	rm -rf $(BUILD)
