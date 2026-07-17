/* NEXUS-OS BOOT — parsing de la structure d'information Multiboot2 (MBI). */
#pragma once

#include "kc/types.h"

#define MB2_BOOTLOADER_MAGIC 0x36d76289u

#define MB2_TAG_END         0
#define MB2_TAG_MMAP        6
#define MB2_TAG_FRAMEBUFFER 8
#define MB2_TAG_ACPI_OLD    14  /* RSDP v1 (20 octets) */
#define MB2_TAG_ACPI_NEW    15  /* RSDP v2 (>= 24 octets) */

struct mb2_tag {
    u32 type;
    u32 size;
} __attribute__((packed));

struct mb2_mmap_entry {
    u64 addr;
    u64 len;
    u32 type;   /* 1 = usable, 3 = ACPI reclaim, 4 = ACPI NVS, 5 = bad, autres = reserved */
    u32 zero;
} __attribute__((packed));

struct mb2_tag_mmap {
    u32 type;
    u32 size;
    u32 entry_size;
    u32 entry_version;
    struct mb2_mmap_entry entries[];
} __attribute__((packed));

/* Renvoie un pointeur vers les octets bruts du RSDP contenu dans un tag ACPI
 * (nouveau prioritaire), ou NULL si absent. */
const void *mb2_find_rsdp(const void *mbi);

struct mb2_tag_framebuffer {
    u32 type;
    u32 size;
    u64 addr;        /* adresse physique du framebuffer */
    u32 pitch;       /* octets par ligne */
    u32 width;
    u32 height;
    u8  bpp;         /* bits par pixel */
    u8  fb_type;     /* 1 = RGB direct color */
    u16 reserved;
    /* pour fb_type==1 : positions/tailles des champs couleur */
    u8  red_pos;   u8 red_size;
    u8  green_pos; u8 green_size;
    u8  blue_pos;  u8 blue_size;
} __attribute__((packed));

/* Renvoie le tag memory-map, ou NULL. */
const struct mb2_tag_mmap *mb2_find_mmap(const void *mbi);

/* Renvoie le tag framebuffer, ou NULL. */
const struct mb2_tag_framebuffer *mb2_find_framebuffer(const void *mbi);
