/* NEXUS-OS IPC/doorbell — signalisation inter-noeuds par IPI (sonnette).
 * Le seul IPI inter-domaine légitime (cf. ARCHITECTURE.md §3.3, §4.4). */
#pragma once

#include "kc/types.h"

#define IPC_DOORBELL_VECTOR 0x41

/* Envoie un IPI "fixed" (vecteur donné) au cœur d'APIC ID `apic_id`. */
void doorbell_ring(u32 apic_id, u8 vector);
