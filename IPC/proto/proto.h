/* NEXUS-OS IPC/proto — ABI binaire figée et versionnée des messages inter-noeuds.
 * Compilée séparément par chaque noeud : toute évolution passe par `version`. */
#pragma once

#include "kc/types.h"

#define IPC_RING_MAGIC  0x3052584Eu   /* "NXR0" (little-endian) */
#define IPC_ABI_VERSION 1u
#define IPC_SLOT_SIZE   64u           /* octets par slot (fixe) */
#define IPC_PAYLOAD_MAX (IPC_SLOT_SIZE - 8u)

/* Message applicatif : occupe exactement un slot. */
struct ipc_msg {
    u32 seq;                    /* numéro de séquence (vérifie l'ordre FIFO) */
    u32 len;                    /* octets utiles dans data[] */
    u8  data[IPC_PAYLOAD_MAX];  /* charge utile */
};
_Static_assert(sizeof(struct ipc_msg) == IPC_SLOT_SIZE, "ipc_msg doit faire un slot");
