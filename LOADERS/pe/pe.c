/* NEXUS-OS — chargeur PE/COFF (PE32+).
 * Toute donnée d'en-tête est validée/bornée avant usage (fail-closed). */
#include "pe.h"
#include "kc/string.h"
#include "serial.h"

struct dos_header {
    u16 e_magic;        /* "MZ" = 0x5A4D */
    u8  pad[58];
    u32 e_lfanew;       /* offset du PE header */
} __attribute__((packed));

struct coff_header {
    u32 signature;      /* "PE\0\0" = 0x00004550 */
    u16 machine;        /* 0x8664 = x86-64 */
    u16 num_sections;
    u32 timestamp;
    u32 sym_ptr;
    u32 num_syms;
    u16 opt_header_size;
    u16 characteristics;
} __attribute__((packed));

struct opt_header64 {
    u16 magic;          /* 0x20B = PE32+ */
    u8  major_linker, minor_linker;
    u32 size_code, size_init, size_uninit;
    u32 entry_point;    /* RVA */
    u32 base_code;
    u64 image_base;
    u32 section_align;
    u32 file_align;
    u16 major_os, minor_os, major_img, minor_img, major_sub, minor_sub;
    u32 win32_version;
    u32 size_image;
    u32 size_headers;
    u32 checksum;
    u16 subsystem;
    u16 dll_characteristics;
    u64 stack_reserve, stack_commit, heap_reserve, heap_commit;
    u32 loader_flags;
    u32 num_rva;
    struct { u32 rva; u32 size; } data_dir[16];
} __attribute__((packed));

struct section_header {
    char name[8];
    u32  virtual_size;
    u32  virtual_addr;   /* RVA */
    u32  raw_size;
    u32  raw_ptr;        /* offset fichier */
    u32  reloc_ptr, linenum_ptr;
    u16  num_reloc, num_linenum;
    u32  characteristics;
} __attribute__((packed));

#define DIR_BASERELOC 5
#define REL_BASED_ABSOLUTE 0
#define REL_BASED_DIR64    10

u64 pe_load(const u8 *image, u64 size, u64 win_lo, u64 win_hi) {
    if (size < sizeof(struct dos_header)) { serial_printf("[pe] image trop petite\n"); return 0; }
    const struct dos_header *dos = (const struct dos_header *)image;
    if (dos->e_magic != 0x5A4D) { serial_printf("[pe] pas de signature MZ\n"); return 0; }
    if (dos->e_lfanew + sizeof(struct coff_header) > size) { serial_printf("[pe] e_lfanew hors image\n"); return 0; }

    const struct coff_header *coff = (const struct coff_header *)(image + dos->e_lfanew);
    if (coff->signature != 0x00004550) { serial_printf("[pe] pas de signature PE\n"); return 0; }
    if (coff->machine != 0x8664) { serial_printf("[pe] machine != x86-64\n"); return 0; }

    u64 opt_off = dos->e_lfanew + sizeof(struct coff_header);
    if (opt_off + sizeof(struct opt_header64) > size) { serial_printf("[pe] optional header hors image\n"); return 0; }
    const struct opt_header64 *opt = (const struct opt_header64 *)(image + opt_off);
    if (opt->magic != 0x20B) { serial_printf("[pe] pas PE32+\n"); return 0; }

    u64 base = opt->image_base;    /* on charge à ImageBase */
    if (base < win_lo || base + opt->size_image > win_hi || base + opt->size_image < base) {
        serial_printf("[pe] image hors fenêtre Node-W (base=0x%lx size=0x%x)\n", base, opt->size_image);
        return 0;
    }

    /* en-têtes -> zone image (utile pour la lecture par le programme le cas échéant) */
    const struct section_header *sec =
        (const struct section_header *)(image + opt_off + coff->opt_header_size);
    if ((const u8 *)(sec + coff->num_sections) > image + size) { serial_printf("[pe] section headers hors image\n"); return 0; }

    for (u16 i = 0; i < coff->num_sections; i++) {
        u64 va  = base + sec[i].virtual_addr;
        u64 vsz = sec[i].virtual_size;
        u64 rsz = sec[i].raw_size;
        u64 rpt = sec[i].raw_ptr;

        if (va < win_lo || va + vsz > win_hi || va + vsz < va) { serial_printf("[pe] section hors fenêtre\n"); return 0; }
        if (rsz) {
            if (rpt + rsz > size || rpt + rsz < rpt) { serial_printf("[pe] section raw hors image\n"); return 0; }
            u64 n = rsz < vsz ? rsz : vsz;
            memcpy((void *)(uintptr_t)va, image + rpt, n);
        }
        if (vsz > rsz) memset((void *)(uintptr_t)(va + rsz), 0, vsz - rsz);   /* bss */

        char nm[9]; memcpy(nm, sec[i].name, 8); nm[8] = 0;
        serial_printf("[pe] section %s VA=0x%lx vsz=%lu rsz=%lu\n", nm, va, vsz, rsz);
    }

    /* relocations de base (delta = base - ImageBase ; ici 0, code présent pour robustesse) */
    i64 delta = (i64)base - (i64)opt->image_base;
    if (delta != 0 && opt->num_rva > DIR_BASERELOC && opt->data_dir[DIR_BASERELOC].size) {
        u64 reloc_rva = opt->data_dir[DIR_BASERELOC].rva;
        u64 reloc_sz  = opt->data_dir[DIR_BASERELOC].size;
        u64 off = 0;
        while (off + 8 <= reloc_sz) {
            const u8 *blk = (const u8 *)(uintptr_t)(base + reloc_rva + off);
            u32 page_rva = *(const u32 *)blk;
            u32 blk_sz   = *(const u32 *)(blk + 4);
            if (blk_sz < 8 || off + blk_sz > reloc_sz) break;
            u32 count = (blk_sz - 8) / 2;
            const u16 *ent = (const u16 *)(blk + 8);
            for (u32 e = 0; e < count; e++) {
                u16 type = ent[e] >> 12, offs = ent[e] & 0xFFF;
                if (type == REL_BASED_DIR64) {
                    u64 *patch = (u64 *)(uintptr_t)(base + page_rva + offs);
                    *patch += (u64)delta;
                } /* REL_BASED_ABSOLUTE = padding, ignoré */
            }
            off += blk_sz;
        }
        serial_printf("[pe] relocations appliquées (delta=0x%lx)\n", (u64)delta);
    }

    u64 entry = base + opt->entry_point;
    if (entry < win_lo || entry >= win_hi) { serial_printf("[pe] entry hors fenêtre\n"); return 0; }
    serial_printf("[pe] PE OK: base=0x%lx entry=0x%lx sections=%u\n", base, entry, coff->num_sections);
    return entry;
}
