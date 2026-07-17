/* NEXUS-OS IPC/channels — canal I/O croisé (split-driver storage).
 * Front-end (Node-W) émet des requêtes ; back-end (Node-L) les sert. Contrat neutre :
 * aucun des deux nœuds ne dépend de l'autre, seulement de ce module partagé. */
#pragma once

#include "kc/types.h"

#define IO_OP_WRITE 1
#define IO_OP_READ  2

#define IO_DISK_BLOCKS 64
#define IO_BLOCK_SIZE  32     /* tient dans un slot ipc_msg (payload 56 o) */

void io_channel_init(void);

/* --- Front-end (appelé par Node-W) --- */
/* Écrit `len` octets à `lba` via le back-end. Renvoie le checksum calculé par le
 * back-end sur les données reçues, ou (u32)-1 en cas d'erreur/timeout. Non bloquant
 * borné (TSC). */
u32 io_client_write(u32 lba, const u8 *buf, u32 len);

/* --- Back-end (appelé par Node-L dans sa boucle idle) --- */
/* Traite au plus une requête en attente. Renvoie true si une requête a été servie.
 * Valide/borne tout champ venant de Node-W (fail-closed). */
bool io_backend_poll(void);
