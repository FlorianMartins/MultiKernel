/* NEXUS-OS IPC/ring — implémentation SPSC lock-free.
 * Correct sur x86 (TSO) grâce à l'appariement release (producteur) / acquire
 * (consommateur) aux points de publication des indices. Compilable hôte + noyau. */
#include "ring.h"

void ipc_ring_init(struct ipc_ring *r) {
    r->magic      = IPC_RING_MAGIC;
    r->version    = IPC_ABI_VERSION;
    r->slot_size  = IPC_SLOT_SIZE;
    r->slot_count = IPC_RING_SLOTS;
    r->cached_tail = 0;
    r->cached_head = 0;
    __atomic_store_n(&r->producer_head, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&r->consumer_tail, 0u, __ATOMIC_RELAXED);
}

bool ipc_ring_push(struct ipc_ring *r, const struct ipc_msg *m) {
    u32 head = __atomic_load_n(&r->producer_head, __ATOMIC_RELAXED);

    /* Plein ? On teste d'abord contre le tail CACHÉ (pas de lecture cross-cœur).
     * On ne relit l'indice réel du consommateur QUE si le cache dit "plein". */
    if ((u32)(head - r->cached_tail) >= r->slot_count) {
        r->cached_tail = __atomic_load_n(&r->consumer_tail, __ATOMIC_ACQUIRE);
        if ((u32)(head - r->cached_tail) >= r->slot_count)
            return false;                               /* réellement plein */
    }

    r->slots[head & (IPC_RING_SLOTS - 1u)] = *m;
    __atomic_store_n(&r->producer_head, head + 1u, __ATOMIC_RELEASE);
    return true;
}

bool ipc_ring_pop(struct ipc_ring *r, struct ipc_msg *out) {
    u32 tail = __atomic_load_n(&r->consumer_tail, __ATOMIC_RELAXED);

    /* Vide ? On teste contre le head CACHÉ ; on ne relit l'indice réel du producteur
     * que si le cache dit "vide" -> moins de trafic MESI en régime chargé. */
    if (tail == r->cached_head) {
        r->cached_head = __atomic_load_n(&r->producer_head, __ATOMIC_ACQUIRE);
        if (tail == r->cached_head)
            return false;                               /* réellement vide */
    }

    *out = r->slots[tail & (IPC_RING_SLOTS - 1u)];
    __atomic_store_n(&r->consumer_tail, tail + 1u, __ATOMIC_RELEASE);
    return true;
}
