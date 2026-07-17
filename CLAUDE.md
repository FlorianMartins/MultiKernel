# NEXUS-OS : Multikernel Architecture Project

## Project Identity
A native asymmetric multikernel (microkernel coordinator) running two isolated core types:
- Node-L: A minimal Monolithic Linux-based service core (POSIX).
- Node-W: An NT-compatible reverse-engineered core (PE/COFF Executable loader and Win32/Kernel subsystem emulation).

## Tech Stack & Target
- Language: C, C++, Assembly (x86_64)
- Environment: Bare-metal / Bootloader (GRUB/Multiboot2)
- Toolchain: gcc, clang, nasm, make
- Emulator for Testing: QEMU (x86_64)

## Development Workflow
- Always draft architectural design specifications in `DOCS/` before coding.
- Write strict unit-tests executable via QEMU or local mock runners.
