/* NEXUS-OS IPC/ring — ring buffer SPSC lock-free (un producteur, un consommateur).
 * Aucun CAS : seulement load/store d'indices avec barrières acquire/release.
 * Indices sur lignes de cache séparées (anti false-sharing). */
#pragma once

#include "kc/types.h"
#include "proto.h"

#define IPC_RING_SLOTS 1024u          /* puissance de 2 */

struct ipc_ring {
    u32 magic;
    u32 version;
    u32 slot_size;
    u32 slot_count;

    /* Indices partagés, chacun sur sa propre ligne de cache (anti false-sharing). */
    _Alignas(64) volatile u32 producer_head;  /* écrit par le PRODUCTEUR seul */
    _Alignas(64) volatile u32 consumer_tail;  /* écrit par le CONSOMMATEUR seul */

    /* Caches privés (technique LMAX Disruptor) : évitent de relire l'indice de
     * l'autre cœur à chaque opération -> moins de trafic de cohérence MESI.
     * cached_tail sur la ligne du producteur, cached_head sur celle du consommateur. */
    _Alignas(64) u32 cached_tail;             /* vu privé du producteur */
    _Alignas(64) u32 cached_head;             /* vu privé du consommateur */

    _Alignas(64) struct ipc_msg slots[IPC_RING_SLOTS];
};

void ipc_ring_init(struct ipc_ring *r);

/* Renvoie false si le ring est plein (jamais bloquant). */
bool ipc_ring_push(struct ipc_ring *r, const struct ipc_msg *m);

/* Renvoie false si le ring est vide. */
bool ipc_ring_pop(struct ipc_ring *r, struct ipc_msg *out);
