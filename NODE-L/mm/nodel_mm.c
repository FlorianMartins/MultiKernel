/* NEXUS-OS Node-L — pagination propre.
 * [0,32MiB) superviseur (image noyau, identité) ; fenêtre user (U=1) pour le ring 3.
 * Tables statiques (BSS, identité) -> adresse = physique. */
#include "nodel_mm.h"
#include "kc/string.h"

#define PTE_P (1ull << 0)
#define PTE_W (1ull << 1)
#define PTE_U (1ull << 2)   /* accessible ring 3 */
#define PTE_PS (1ull << 7)  /* page 2 MiB */

#define MiB (1024ull * 1024ull)
#define KERNEL_ID_LIMIT (32ull * MiB)

static u64 nodel_pml4[512] __attribute__((aligned(4096)));
static u64 nodel_pdpt[512] __attribute__((aligned(4096)));
static u64 nodel_pd[512]   __attribute__((aligned(4096)));

static void map_2m(u64 base, u64 size, u64 flags) {
    for (u64 off = 0; off < size; off += 2 * MiB) {
        u64 pa = base + off;
        u64 idx = pa / (2 * MiB);
        if (idx < 512) nodel_pd[idx] = pa | flags | PTE_PS;
    }
}

u64 nodel_mm_activate(void) {
    memset(nodel_pml4, 0, 4096);
    memset(nodel_pdpt, 0, 4096);
    memset(nodel_pd, 0, 4096);

    /* image noyau : superviseur (U=0) — le ring 3 ne peut pas y toucher */
    map_2m(0, KERNEL_ID_LIMIT, PTE_P | PTE_W);
    /* fenêtre user : accessible ring 3 (U=1) */
    map_2m(NODEL_USER_WIN_BASE, NODEL_USER_WIN_SIZE, PTE_P | PTE_W | PTE_U);

    nodel_pdpt[0] = (u64)(uintptr_t)nodel_pd   | PTE_P | PTE_W | PTE_U;
    nodel_pml4[0] = (u64)(uintptr_t)nodel_pdpt | PTE_P | PTE_W | PTE_U;

    u64 cr3 = (u64)(uintptr_t)nodel_pml4;
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
    return cr3;
}
