/* NEXUS-OS — chargeur ELF64.
 * Toute donnée de l'en-tête est validée avant usage (fail-closed) : offsets/tailles
 * bornés contre l'image source ET la fenêtre de destination. */
#include "elf.h"
#include "kc/string.h"
#include "serial.h"

struct elf64_ehdr {
    u8  e_ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} __attribute__((packed));

struct elf64_phdr {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
} __attribute__((packed));

#define PT_LOAD 1
#define ET_EXEC 2
#define EM_X86_64 62

u64 elf_load(const u8 *image, u64 size, u64 win_lo, u64 win_hi) {
    if (size < sizeof(struct elf64_ehdr)) { serial_printf("[elf] image trop petite\n"); return 0; }
    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)image;

    if (!(eh->e_ident[0] == 0x7F && eh->e_ident[1] == 'E' &&
          eh->e_ident[2] == 'L' && eh->e_ident[3] == 'F')) {
        serial_printf("[elf] magic invalide\n"); return 0;
    }
    if (eh->e_ident[4] != 2 /*ELFCLASS64*/ || eh->e_type != ET_EXEC ||
        eh->e_machine != EM_X86_64) {
        serial_printf("[elf] classe/type/machine non supportés\n"); return 0;
    }
    if (eh->e_phoff + (u64)eh->e_phnum * sizeof(struct elf64_phdr) > size) {
        serial_printf("[elf] table de programme hors image\n"); return 0;
    }

    for (u16 i = 0; i < eh->e_phnum; i++) {
        const struct elf64_phdr *ph =
            (const struct elf64_phdr *)(image + eh->e_phoff + (u64)i * sizeof(*ph));
        if (ph->p_type != PT_LOAD) continue;

        /* bornes source */
        if (ph->p_offset + ph->p_filesz < ph->p_offset) return 0;
        if (ph->p_offset + ph->p_filesz > size) { serial_printf("[elf] segment hors image\n"); return 0; }
        /* bornes destination (fenêtre user) */
        if (ph->p_memsz < ph->p_filesz) return 0;
        if (ph->p_vaddr < win_lo) { serial_printf("[elf] vaddr sous la fenêtre\n"); return 0; }
        if (ph->p_vaddr + ph->p_memsz < ph->p_vaddr) return 0;
        if (ph->p_vaddr + ph->p_memsz > win_hi) { serial_printf("[elf] vaddr hors fenêtre\n"); return 0; }

        u8 *dst = (u8 *)(uintptr_t)ph->p_vaddr;
        memcpy(dst, image + ph->p_offset, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz)
            memset(dst + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);   /* .bss */

        serial_printf("[elf] PT_LOAD vaddr=0x%lx filesz=%lu memsz=%lu\n",
                      ph->p_vaddr, ph->p_filesz, ph->p_memsz);
    }

    if (eh->e_entry < win_lo || eh->e_entry >= win_hi) { serial_printf("[elf] entry hors fenêtre\n"); return 0; }
    return eh->e_entry;
}
