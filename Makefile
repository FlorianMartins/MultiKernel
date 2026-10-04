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
            -IHAL/iommu -IHAL/apic -IHAL/input -IHAL/gpu -IBOOT/stage2 -ICOORDINATOR/topology \
            -ICOORDINATOR/mm -ICOORDINATOR/smp -ICOORDINATOR/monitor -ICOORDINATOR/bench \
            -IIPC/proto -IIPC/ring -IIPC/doorbell -IIPC/channels \
            -INODE-L/kernel -INODE-L/mm -INODE-L/sched -INODE-L/syscall -ILOADERS/elf \
            -INODE-W/kernel -INODE-W/executive -ILOADERS/pe

# Flags de fault-injection (tests Phase 6). Vides en build nominal.
NODEW_FAULT_ONCE ?=
NODEL_WX_TEST ?=
HARDEN_DEFS := $(if $(NODEW_FAULT_ONCE),-DNODEW_FAULT_ONCE,) $(if $(NODEL_WX_TEST),-DNODEL_WX_TEST,) $(if $(GFX_DEMO),-DGFX_DEMO,) $(if $(NODEL_GUI),-DNODEL_GUI,)

# Freestanding, sans pile rouge, sans SSE/MMX/x87 (CR4.OSFXSR non configuré),
# modèle mémoire "small" (noyau en < 2 GiB), non-PIE.
# Optimisation (Phase 7) : -O3 + déroulage + ordonnancement générique moderne.
# On reste -mgeneral-regs-only (pas de SSE/AVX : CR4.OSFXSR non configuré -> sûr).
OPT := -O3 -funroll-loops -finline-functions -mtune=generic
CFLAGS := -ffreestanding -nostdlib -fno-stack-protector -fno-pic -fno-pie \
          -mno-red-zone -mgeneral-regs-only -mcmodel=small \
          -std=gnu11 $(OPT) -Wall -Wextra \
          -fno-builtin -fno-tree-loop-distribute-patterns \
          -fno-asynchronous-unwind-tables $(HARDEN_DEFS) $(INCLUDES)

LDFLAGS  := -n -z max-page-size=0x1000 -T TOOLS/image/linker.ld
ASMFLAGS := -f elf64

C_SRC := \
    LIBS/libkc/string.c \
    LIBS/libkc/printf.c \
    HAL/serial/serial.c \
    HAL/acpi/acpi.c \
    HAL/iommu/iommu.c \
    HAL/apic/ioapic.c \
    HAL/input/ps2.c \
    HAL/input/ps2mouse.c \
    HAL/gpu/fb.c \
    HAL/gpu/splash.c \
    HAL/gpu/gfxdemo.c \
    BOOT/stage2/multiboot2.c \
    COORDINATOR/topology/topology.c \
    COORDINATOR/monitor/monitor.c \
    COORDINATOR/bench/bench.c \
    COORDINATOR/mm/mm.c \
    COORDINATOR/smp/smp.c \
    COORDINATOR/smp/tramp_blob.c \
    IPC/ring/ring.c \
    IPC/doorbell/doorbell.c \
    IPC/channels/io_channel.c \
    NODE-L/mm/nodel_mm.c \
    NODE-L/sched/sched.c \
    NODE-L/syscall/syscall_dispatch.c \
    NODE-L/kernel/gdt.c \
    NODE-L/kernel/nodel_idt.c \
    NODE-L/kernel/nodel.c \
    NODE-L/userland/user_blob.c \
    NODE-W/kernel/nodew_mm.c \
    NODE-W/kernel/nodew_gdt.c \
    NODE-W/kernel/nodew_idt.c \
    NODE-W/kernel/nodew.c \
    NODE-W/executive/nt_dispatch.c \
    NODE-W/subsystems/pe_blob.c \
    LOADERS/elf/elf.c \
    LOADERS/pe/pe.c \
    COORDINATOR/core/main.c

ASM_SRC := BOOT/stage2/boot.asm COORDINATOR/smp/isr.asm HAL/input/irq.asm \
           LIBS/librt/arch.asm NODE-L/kernel/entry.asm NODE-W/kernel/nt_entry.asm

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

# --- userland Node-L : ELF64 statique freestanding, lié dans la fenêtre user ---
USER_ELF := $(BUILD)/nodel_user.elf
UCFLAGS  := -ffreestanding -nostdlib -fno-pic -fno-pie -mno-red-zone \
            -mgeneral-regs-only -fno-stack-protector -std=gnu11 -O2 -Wall -Wextra \
            -fno-asynchronous-unwind-tables $(HARDEN_DEFS) -INODE-L/syscall -IHAL/gpu \
            -ILIBS/libkc/include
# Le compositeur (gui.c) n'est lié que pour le build GUI.
USER_SRC := NODE-L/userland/init.c $(if $(NODEL_GUI),NODE-L/userland/gui.c,)

$(USER_ELF): $(USER_SRC) NODE-L/userland/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c NODE-L/userland/init.c -o $(BUILD)/nodel_user_init.o
	$(if $(NODEL_GUI),$(CC) $(UCFLAGS) -c NODE-L/userland/gui.c -o $(BUILD)/nodel_user_gui.o,)
	$(LD) -n -T NODE-L/userland/user.ld -o $@ $(BUILD)/nodel_user_init.o \
	    $(if $(NODEL_GUI),$(BUILD)/nodel_user_gui.o,)
	@echo "==> built userland $@ $(if $(NODEL_GUI),[GUI],)"

# user_blob.c fait un .incbin de build/nodel_user.elf -> dépendance explicite
$(BUILD)/NODE-L/userland/user_blob.o: $(USER_ELF)

# --- exécutable PE Node-W : PE32+ fabriqué à la main (nasm -f bin) ---
# NODEW_CRASH=1 -> variante qui déréférence la RAM Node-L (test de confinement).
PE_EXE := $(BUILD)/hello_pe.exe
PE_DEF := $(if $(NODEW_CRASH),-dNODEW_CRASH,)

$(PE_EXE): NODE-W/subsystems/hello_pe.asm
	@mkdir -p $(dir $@)
	$(ASM) -f bin $(PE_DEF) $< -o $@
	@echo "==> built PE $@ $(if $(NODEW_CRASH),[CRASH mode],)"

# pe_blob.c fait un .incbin de build/hello_pe.exe -> dépendance explicite
$(BUILD)/NODE-W/subsystems/pe_blob.o: $(PE_EXE)

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

# Validation automatisée (CI archi + unitaire hôte + QEMU Phase 6 + clavier PS/2).
test: ci test-unit test-phase6 test-phase8 test-phase9

test-phase1: $(ISO)
	@SMP=$(SMP) MEM=$(MEM) ISO=$(ISO) bash TESTS/qemu/run_phase1.sh

test-phase2: $(ISO)
	@SMP=$(SMP) MEM=$(MEM) ISO=$(ISO) bash TESTS/qemu/run_phase2.sh

test-phase3: $(ISO)
	@SMP=$(SMP) MEM=$(MEM) ISO=$(ISO) bash TESTS/qemu/run_phase3.sh

test-phase4: $(ISO)
	@SMP=$(SMP) MEM=$(MEM) ISO=$(ISO) bash TESTS/qemu/run_phase4.sh

test-phase5: $(ISO)
	@SMP=$(SMP) MEM=$(MEM) ISO=$(ISO) bash TESTS/qemu/run_phase5.sh

test-phase6: $(ISO)
	@SMP=$(SMP) MEM=$(MEM) ISO=$(ISO) bash TESTS/qemu/run_phase6.sh

test-phase8: $(ISO)
	@SMP=$(SMP) MEM=$(MEM) ISO=$(ISO) bash TESTS/qemu/run_phase8_kbd.sh

test-phase9: $(ISO)
	@SMP=$(SMP) MEM=$(MEM) ISO=$(ISO) bash TESTS/qemu/run_phase9_fb.sh

# Souris PS/2 + double buffering (rebuild GFX_DEMO=1 en interne, restaure le nominal).
test-phase10:
	@SMP=$(SMP) MEM=$(MEM) bash TESTS/qemu/run_phase10_mouse.sh

test-phase11:
	@SMP=$(SMP) MEM=$(MEM) bash TESTS/qemu/run_phase11_gui.sh

# CI d'architecture : aucune dépendance croisée NODE-L <-> NODE-W.
ci:
	@bash TOOLS/ci/check_deps.sh

# Matrice de tests poussés (cœurs × mémoire + stress).
test-matrix: $(ISO)
	@bash TESTS/qemu/matrix.sh

# Test unitaire hôte du ring SPSC (pthreads, 10^7 messages).
test-unit:
	@$(CC) -O2 -pthread -ILIBS/libkc/include -IIPC/proto -IIPC/ring \
	    TESTS/unit/test_ring.c IPC/ring/ring.c -o $(BUILD)/test_ring
	@$(BUILD)/test_ring

# Bench perf du ring sur matériel réel (hôte) : débit + cycles/msg cross-cœur.
bench-host:
	@$(CC) -O2 -pthread -ILIBS/libkc/include -IIPC/proto -IIPC/ring \
	    TESTS/unit/bench_ring.c IPC/ring/ring.c -o $(BUILD)/bench_ring
	@$(BUILD)/bench_ring

# Banc de test web : serveur QEMU + console série diffusée au navigateur.
WEB_PORT ?= 8080
web: $(ISO)
	@echo "MultiKernel web tester -> http://127.0.0.1:$(WEB_PORT)  (Ctrl-C pour arrêter)"
	@python3 WEB/server.py --port $(WEB_PORT) --smp $(SMP) --mem $(MEM)

# Copie l'ISO dans WEB/ pour l'option v86 (100% navigateur).
web-static: $(ISO)
	@cp $(ISO) WEB/nexus-os.iso
	@echo "ISO copiée dans WEB/nexus-os.iso — ajoute les assets v86 dans WEB/v86/ (cf. WEB/README.md)"

clean:
	rm -rf $(BUILD)
