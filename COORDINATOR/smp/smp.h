/* NEXUS-OS COORDINATOR/smp — réveil des AP, isolation (Phase 2) & IPC (Phase 3). */
#pragma once

#include "kc/types.h"
#include "topology.h"

struct smp_result {
    /* démarrage */
    u32  expected;      /* AP à réveiller (cœurs non-COORD, activés, hors BSP) */
    u32  started;       /* AP ayant consommé le trampoline */
    u32  alive;         /* AP ayant signalé leur vivacité */

    /* isolation (Phase 2) */
    u32  iso_expected;  /* AP en rôle "isolation" */
    u32  iso_pass;      /* #PF croisés capturés */
    u32  iso_fail;      /* accès croisés ayant réussi (violation) */

    /* IPC (Phase 3) */
    bool ipc_enabled;   /* un producteur ET un consommateur ont été assignés */
    u32  ipc_expected;  /* messages attendus */
    u32  ipc_got;       /* messages reçus par le consommateur */
    bool ipc_order_ok;  /* ordre FIFO strict respecté */
    bool ipc_sum_ok;    /* checksum exact (0 perte / 0 corruption) */
    bool doorbell_ok;   /* consommateur réveillé du hlt par l'IPI */

    /* Node-L souverain (Phase 4) */
    bool nodel_present;    /* un cœur exécute le noyau Node-L */
    bool nodel_alive;      /* heartbeat de Node-L observé en progression par le BSP */
    u64  nodel_heartbeat;  /* dernière valeur lue */

    /* Node-W souverain (Phase 5) */
    bool nodew_present;    /* un cœur exécute le noyau Node-W */
    bool nodew_alive;      /* heartbeat de Node-W observé en progression */
    u64  nodew_heartbeat;
    bool nodew_io_ok;      /* round-trip I/O croisé vérifié */
    bool nodew_terminated; /* le PE a fait NtTerminateProcess */

    /* Résilience (Phase 6) */
    bool nodew_restarted;         /* le Coordinator a dû redémarrer Node-W à chaud */
    bool nodel_survived_restart;  /* Node-L est resté vivant pendant le restart de W */
};

struct smp_result smp_boot_aps(const struct topology *t, u64 lapic_base);
