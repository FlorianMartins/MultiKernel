/* NEXUS-OS COORDINATOR/mm — partitionnement RAM & tables de pages par domaine. */
#pragma once

#include "kc/types.h"
#include "topology.h"

#define MiB (1024ull * 1024ull)

/* Plan mémoire Phase 2 (identité, huge pages 2 MiB). */
#define MM_SHARED_LIMIT (32ull  * MiB)   /* [0, 32MiB) partagé par tous les domaines */
#define MM_NODE_L_BASE  (64ull  * MiB)   /* fenêtre Node-L */
#define MM_NODE_W_BASE  (128ull * MiB)   /* fenêtre Node-W */
#define MM_WINDOW_SIZE  (32ull  * MiB)

enum domain { DOM_COORD = 0, DOM_NODE_L = 1, DOM_NODE_W = 2 };

const char  *mm_domain_name(enum domain d);
enum domain  mm_domain_of(const struct topology *t, u32 index);
u64          mm_domain_window_base(enum domain d);

/* Construit (ou renvoie) le CR3 physique du domaine. 0 pour COORD (garde le CR3 boot). */
u64 mm_build_domain_cr3(enum domain d);
