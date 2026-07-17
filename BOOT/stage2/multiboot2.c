/* NEXUS-OS BOOT — parcours des tags Multiboot2. */
#include "multiboot2.h"

/* La MBI débute par { u32 total_size; u32 reserved; } puis une suite de tags,
 * chacun aligné sur 8 octets, terminée par un tag de type 0. */
static const struct mb2_tag *mb2_first(const void *mbi) {
    return (const struct mb2_tag *)((const u8 *)mbi + 8);
}

static const struct mb2_tag *mb2_next(const struct mb2_tag *t) {
    u32 adv = (t->size + 7u) & ~7u; /* arrondi à 8 octets */
    return (const struct mb2_tag *)((const u8 *)t + adv);
}

static const struct mb2_tag *mb2_find(const void *mbi, u32 type) {
    for (const struct mb2_tag *t = mb2_first(mbi);
         t->type != MB2_TAG_END;
         t = mb2_next(t)) {
        if (t->type == type) return t;
    }
    return 0;
}

const void *mb2_find_rsdp(const void *mbi) {
    const struct mb2_tag *t = mb2_find(mbi, MB2_TAG_ACPI_NEW);
    if (!t) t = mb2_find(mbi, MB2_TAG_ACPI_OLD);
    if (!t) return 0;
    /* Les octets du RSDP suivent immédiatement l'en-tête { type, size }. */
    return (const u8 *)t + 8;
}

const struct mb2_tag_mmap *mb2_find_mmap(const void *mbi) {
    return (const struct mb2_tag_mmap *)mb2_find(mbi, MB2_TAG_MMAP);
}
