/* NEXUS-OS COORDINATOR/mm — tables de pages par domaine.
 * Chaque domaine mappe en identité la région partagée [0,32MiB) plus SA fenêtre RAM ;
 * tout le reste est non-présent -> un accès croisé provoque un #PF (preuve d'isolation).
 * Les tables sont statiques (BSS, identité) : leur adresse = adresse physique. */
#include "mm.h"
#include "kc/string.h"

#define PTE_P   (1ull << 0)
#define PTE_W   (1ull << 1)
#define PTE_PWT (1ull << 3)  /* write-through */
#define PTE_PCD (1ull << 4)  /* cache disable (UC) — pour la MMIO */
#define PTE_PS  (1ull << 7)  /* page 2 MiB */

#define LAPIC_PHYS 0xFEE00000ull

static u64 pml4_L[512] __attribute__((aligned(4096)));
static u64 pdpt_L[512] __attribute__((aligned(4096)));
static u64 pd_L[512]   __attribute__((aligned(4096)));

static u64 pml4_W[512] __attribute__((aligned(4096)));
static u64 pdpt_W[512] __attribute__((aligned(4096)));
static u64 pd_W[512]   __attribute__((aligned(4096)));

/* PD partagé pour le 4e GiB : mappe la MMIO LAPIC (UC) dans chaque domaine.
 * Nécessaire pour que les cœurs AP émettent des IPI (doorbell) et fassent l'EOI.
 * N'affaiblit pas l'isolation : le LAPIC est per-CPU, pas de la RAM d'un autre noeud. */
static u64 pd_lapic[512] __attribute__((aligned(4096)));

const char *mm_domain_name(enum domain d) {
    switch (d) {
    case DOM_COORD:  return "COORD";
    case DOM_NODE_L: return "NODE_L";
    default:         return "NODE_W";
    }
}

enum domain mm_domain_of(const struct topology *t, u32 index) {
    /* TODO(sécurité, Phase 2.5, cf. DOCS/security-model.md §4) : ce split par index
     * ne décode pas encore la topologie cœur/thread (CPUID.0BH/1FH). Il peut placer
     * deux domaines sur des threads SMT frères du même cœur physique -> canal auxiliaire
     * (cache/SMT) malgré l'isolation RAM. Règle cible : jamais deux domaines sur des
     * hyperthreads frères. */
    if (index == 0) return DOM_COORD;
    u32 rest = (t->cpu_count >= 1) ? t->cpu_count - 1 : 0;
    u32 nl = (rest + 1) / 2;                 /* Node-L prend le surplus (ceil) */
    return (index <= nl) ? DOM_NODE_L : DOM_NODE_W;
}

u64 mm_domain_window_base(enum domain d) {
    if (d == DOM_NODE_L) return MM_NODE_L_BASE;
    if (d == DOM_NODE_W) return MM_NODE_W_BASE;
    return 0;
}

/* Mappe [base, base+size) en identité, huge pages 2 MiB, dans le PD (low 1 GiB). */
static void map_2m_range(u64 *pd, u64 base, u64 size) {
    for (u64 off = 0; off < size; off += 2 * MiB) {
        u64 pa = base + off;
        u64 idx = pa / (2 * MiB);
        if (idx < 512) pd[idx] = pa | PTE_P | PTE_W | PTE_PS;
    }
}

/* Mappe la MMIO LAPIC (UC) via PDPT[3] -> pd_lapic partagé. */
static void map_lapic(u64 *pdpt) {
    u64 gib_idx = (LAPIC_PHYS >> 30) & 0x1FF;   /* index PDPT (4e GiB -> 3) */
    u64 pd_idx  = (LAPIC_PHYS >> 21) & 0x1FF;   /* page 2 MiB dans ce GiB */
    pd_lapic[pd_idx] = (LAPIC_PHYS & ~0x1FFFFFull)
                       | PTE_P | PTE_W | PTE_PS | PTE_PCD | PTE_PWT;
    pdpt[gib_idx] = (u64)(uintptr_t)pd_lapic | PTE_P | PTE_W;
}

static u64 build(u64 *pml4, u64 *pdpt, u64 *pd, u64 win_base) {
    memset(pml4, 0, 4096);
    memset(pdpt, 0, 4096);
    memset(pd, 0, 4096);
    map_2m_range(pd, 0, MM_SHARED_LIMIT);      /* région partagée (dont le ring IPC) */
    map_2m_range(pd, win_base, MM_WINDOW_SIZE); /* fenêtre propre du domaine */
    pdpt[0] = (u64)(uintptr_t)pd   | PTE_P | PTE_W;
    map_lapic(pdpt);                            /* MMIO LAPIC (doorbell/EOI) */
    pml4[0] = (u64)(uintptr_t)pdpt | PTE_P | PTE_W;
    return (u64)(uintptr_t)pml4;
}

u64 mm_build_domain_cr3(enum domain d) {
    if (d == DOM_NODE_L) return build(pml4_L, pdpt_L, pd_L, MM_NODE_L_BASE);
    if (d == DOM_NODE_W) return build(pml4_W, pdpt_W, pd_W, MM_NODE_W_BASE);
    return 0;
}
