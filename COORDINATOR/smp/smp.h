/* NEXUS-OS COORDINATOR/smp — réveil des AP & preuve d'isolation (Phase 2). */
#pragma once

#include "kc/types.h"
#include "topology.h"

struct smp_result {
    u32 expected;    /* AP à réveiller (cœurs non-COORD, activés, hors BSP) */
    u32 started;     /* AP ayant consommé le trampoline */
    u32 alive;       /* AP ayant prouvé l'accès à leur propre fenêtre */
    u32 iso_pass;    /* #PF croisés capturés (isolation prouvée) */
    u32 iso_fail;    /* accès croisés ayant réussi (isolation violée) */
};

struct smp_result smp_boot_aps(const struct topology *t, u64 lapic_base);
