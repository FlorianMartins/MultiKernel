/* NEXUS-OS IPC/channels — canal I/O croisé storage (split-driver).
 * Deux rings SPSC : requêtes W->L et réponses L->W. Disque simulé en RAM partagée.
 * SÉCURITÉ : le back-end borne lba/len (fail-closed) et ne déréférence jamais de
 * pointeur venu de l'autre nœud — on ne transporte que des octets copiés dans les slots. */
#include "io_channel.h"
#include "ring.h"
#include "proto.h"
#include "kc/cpu.h"
#include "kc/string.h"

/* Encodage dans un slot ipc_msg :
 *   requête  : seq = op, len = data_len, data = [ lba(4) | payload(len) ]
 *   réponse  : seq = status, len = 4,    data = [ checksum(4) ] */
static struct ipc_ring g_io_req  __attribute__((aligned(64)));
static struct ipc_ring g_io_resp __attribute__((aligned(64)));

static u8 g_sim_disk[IO_DISK_BLOCKS][IO_BLOCK_SIZE];

void io_channel_init(void) {
    ipc_ring_init(&g_io_req);
    ipc_ring_init(&g_io_resp);
    memset(g_sim_disk, 0, sizeof(g_sim_disk));
}

static u32 checksum(const u8 *p, u32 n) {
    u32 s = 0;
    for (u32 i = 0; i < n; i++) s = (s * 31u) + p[i];
    return s;
}

u32 io_client_write(u32 lba, const u8 *buf, u32 len) {
    if (len > IO_BLOCK_SIZE) return (u32)-1;

    struct ipc_msg m;
    memset(&m, 0, sizeof(m));
    m.seq = IO_OP_WRITE;
    m.len = 4 + len;
    if (m.len > IPC_PAYLOAD_MAX) return (u32)-1;
    memcpy(m.data, &lba, 4);
    memcpy(m.data + 4, buf, len);

    if (!ipc_ring_push(&g_io_req, &m)) return (u32)-1;

    /* attend la réponse (borné TSC : le back-end tourne sur un autre cœur) */
    struct ipc_msg r;
    u64 deadline = rdtsc() + 20000000000ull;
    while (!ipc_ring_pop(&g_io_resp, &r)) {
        if (rdtsc() > deadline) return (u32)-1;
        cpu_relax();
    }
    if (r.len < 4) return (u32)-1;
    u32 cksum;
    memcpy(&cksum, r.data, 4);
    return cksum;
}

bool io_backend_poll(void) {
    struct ipc_msg m;
    if (!ipc_ring_pop(&g_io_req, &m)) return false;

    struct ipc_msg r;
    memset(&r, 0, sizeof(r));
    r.len = 4;
    u32 status = 0, cksum = 0;

    /* validation stricte des champs venus de Node-W (fail-closed) */
    if (m.seq == IO_OP_WRITE && m.len >= 4 && m.len <= IPC_PAYLOAD_MAX) {
        u32 lba;
        memcpy(&lba, m.data, 4);
        u32 dlen = m.len - 4;
        if (lba < IO_DISK_BLOCKS && dlen <= IO_BLOCK_SIZE) {
            memcpy(g_sim_disk[lba], m.data + 4, dlen);          /* écrit le "disque" */
            cksum = checksum(g_sim_disk[lba], dlen);            /* checksum du contenu stocké */
            status = 0;
        } else {
            status = 1;   /* hors bornes -> rejeté */
        }
    } else {
        status = 2;       /* opcode/longueur invalides */
    }

    r.seq = status;
    memcpy(r.data, &cksum, 4);
    while (!ipc_ring_push(&g_io_resp, &r)) cpu_relax();          /* réponse (place garantie) */
    return true;
}
