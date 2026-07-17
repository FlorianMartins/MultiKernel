/* NEXUS-OS Node-W — pagination propre.
 * [0,32MiB) superviseur (image noyau + rings IPC partagés) ; fenêtre Node-W user (U=1). */
#include "nodew_mm.h"
#include "kc/string.h"

#define PTE_P  (1ull << 0)
#define PTE_W  (1ull << 1)
#define PTE_U  (1ull << 2)
#define PTE_PS (1ull << 7)

#define MiB (1024ull * 1024ull)
#define KERNEL_ID_LIMIT (32ull * MiB)

static u64 w_pml4[512] __attribute__((aligned(4096)));
static u64 w_pdpt[512] __attribute__((aligned(4096)));
static u64 w_pd[512]   __attribute__((aligned(4096)));

static void map_2m(u64 base, u64 size, u64 flags) {
    for (u64 off = 0; off < size; off += 2 * MiB) {
        u64 pa = base + off;
        u64 idx = pa / (2 * MiB);
        if (idx < 512) w_pd[idx] = pa | flags | PTE_PS;
    }
}

u64 nodew_mm_activate(void) {
    memset(w_pml4, 0, 4096);
    memset(w_pdpt, 0, 4096);
    memset(w_pd, 0, 4096);

    map_2m(0, KERNEL_ID_LIMIT, PTE_P | PTE_W);                       /* noyau superviseur */
    map_2m(NODEW_USER_WIN_BASE, NODEW_USER_WIN_SIZE, PTE_P | PTE_W | PTE_U); /* fenêtre user */

    w_pdpt[0] = (u64)(uintptr_t)w_pd   | PTE_P | PTE_W | PTE_U;
    w_pml4[0] = (u64)(uintptr_t)w_pdpt | PTE_P | PTE_W | PTE_U;

    u64 cr3 = (u64)(uintptr_t)w_pml4;
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
    return cr3;
}
