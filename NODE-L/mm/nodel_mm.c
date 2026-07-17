/* NEXUS-OS Node-L — pagination propre.
 * [0,32MiB) superviseur (image noyau, identité) ; fenêtre user (U=1) pour le ring 3.
 * Tables statiques (BSS, identité) -> adresse = physique. */
#include "nodel_mm.h"
#include "kc/string.h"

#define PTE_P   (1ull << 0)
#define PTE_W   (1ull << 1)
#define PTE_U   (1ull << 2)   /* accessible ring 3 */
#define PTE_PWT (1ull << 3)
#define PTE_PCD (1ull << 4)   /* cache disable (UC) — pour la MMIO */
#define PTE_PS  (1ull << 7)   /* page 2 MiB */
#define PTE_NX  (1ull << 63)  /* No-eXecute (W^X) — nécessite EFER.NXE */

#define MiB (1024ull * 1024ull)
#define KERNEL_ID_LIMIT (32ull * MiB)
#define LAPIC_PHYS 0xFEE00000ull

static u64 nodel_pml4[512]  __attribute__((aligned(4096)));
static u64 nodel_pdpt[512]  __attribute__((aligned(4096)));
static u64 nodel_pd[512]    __attribute__((aligned(4096)));
static u64 nodel_pd_hi[512] __attribute__((aligned(4096)));   /* 4e GiB : MMIO LAPIC */

static void map_2m(u64 base, u64 size, u64 flags) {
    for (u64 off = 0; off < size; off += 2 * MiB) {
        u64 pa = base + off;
        u64 idx = pa / (2 * MiB);
        if (idx < 512) nodel_pd[idx] = pa | flags | PTE_PS;
    }
}

/* Mappe la fenêtre user : W^X à granularité 2 MiB. Seule la page contenant le code
 * chargé est exécutable ; le reste (pile, données) est NX -> pas d'exécution de shellcode. */
static void map_user_wx(u64 base, u64 size, u64 exec_base) {
    u64 exec_page = exec_base & ~(2 * MiB - 1);
    for (u64 off = 0; off < size; off += 2 * MiB) {
        u64 pa = base + off;
        u64 idx = pa / (2 * MiB);
        if (idx >= 512) continue;
        u64 flags = PTE_P | PTE_W | PTE_U;
        if (pa != exec_page) flags |= PTE_NX;   /* tout sauf la page de code = NX */
        nodel_pd[idx] = pa | flags | PTE_PS;
    }
}

u64 nodel_mm_activate(void) {
    memset(nodel_pml4, 0, 4096);
    memset(nodel_pdpt, 0, 4096);
    memset(nodel_pd, 0, 4096);
    memset(nodel_pd_hi, 0, 4096);

    /* image noyau : superviseur (U=0) — le ring 3 ne peut pas y toucher */
    map_2m(0, KERNEL_ID_LIMIT, PTE_P | PTE_W);
    /* fenêtre user : ring 3, W^X (code X, pile/données NX) */
    map_user_wx(NODEL_USER_WIN_BASE, NODEL_USER_WIN_SIZE, NODEL_USER_LOAD_BASE);

    /* MMIO LAPIC (UC) dans le 4e GiB : requis pour l'EOI des IRQ (clavier). */
    nodel_pd_hi[(LAPIC_PHYS >> 21) & 0x1FF] =
        (LAPIC_PHYS & ~0x1FFFFFull) | PTE_P | PTE_W | PTE_PS | PTE_PCD | PTE_PWT | PTE_NX;
    nodel_pdpt[(LAPIC_PHYS >> 30) & 0x1FF] = (u64)(uintptr_t)nodel_pd_hi | PTE_P | PTE_W;

    nodel_pdpt[0] = (u64)(uintptr_t)nodel_pd   | PTE_P | PTE_W | PTE_U;
    nodel_pml4[0] = (u64)(uintptr_t)nodel_pdpt | PTE_P | PTE_W | PTE_U;

    u64 cr3 = (u64)(uintptr_t)nodel_pml4;
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
    return cr3;
}
