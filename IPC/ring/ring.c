/* NEXUS-OS IPC/ring — implémentation SPSC lock-free.
 * Correct sur x86 (TSO) grâce à l'appariement release (producteur) / acquire
 * (consommateur) aux points de publication des indices. Compilable hôte + noyau. */
#include "ring.h"

void ipc_ring_init(struct ipc_ring *r) {
    r->magic      = IPC_RING_MAGIC;
    r->version    = IPC_ABI_VERSION;
    r->slot_size  = IPC_SLOT_SIZE;
    r->slot_count = IPC_RING_SLOTS;
    __atomic_store_n(&r->producer_head, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&r->consumer_tail, 0u, __ATOMIC_RELAXED);
}

bool ipc_ring_push(struct ipc_ring *r, const struct ipc_msg *m) {
    /* Seul le producteur écrit head : lecture relaxed suffisante côté producteur. */
    u32 head = __atomic_load_n(&r->producer_head, __ATOMIC_RELAXED);
    u32 tail = __atomic_load_n(&r->consumer_tail, __ATOMIC_ACQUIRE);

    if ((u32)(head - tail) >= r->slot_count)
        return false;                                   /* plein */

    r->slots[head & (IPC_RING_SLOTS - 1u)] = *m;        /* écrit la charge utile */
    __atomic_store_n(&r->producer_head, head + 1u, __ATOMIC_RELEASE); /* publie */
    return true;
}

bool ipc_ring_pop(struct ipc_ring *r, struct ipc_msg *out) {
    /* Seul le consommateur écrit tail : lecture relaxed suffisante côté consommateur. */
    u32 tail = __atomic_load_n(&r->consumer_tail, __ATOMIC_RELAXED);
    u32 head = __atomic_load_n(&r->producer_head, __ATOMIC_ACQUIRE);

    if (head == tail)
        return false;                                   /* vide */

    *out = r->slots[tail & (IPC_RING_SLOTS - 1u)];       /* lit la charge utile */
    __atomic_store_n(&r->consumer_tail, tail + 1u, __ATOMIC_RELEASE); /* libère */
    return true;
}
