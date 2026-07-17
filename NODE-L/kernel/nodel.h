/* NEXUS-OS Node-L — noyau souverain (Phase 4). */
#pragma once

#include "kc/types.h"

struct nodel_result {
    bool cr3_switched;     /* Node-L a basculé sur sa propre pagination */
    u32  tasks_ran;        /* nb de tâches ordonnancées ayant terminé */
    bool user_ran;         /* le userland a bien tourné en ring 3 */
    bool user_exited;      /* SYS_exit reçu */
    u32  user_exit_code;
    u32  shell_cmds;       /* commandes shell exécutées */
    u64  heartbeat;        /* valeur finale du heartbeat */
};

/* Exécuté par le 1er cœur Node-L. Renvoie le bilan (le BSP l'observe via SHM). */
void node_l_main(void);

extern volatile struct nodel_result g_nodel;
extern volatile u64 g_nodel_heartbeat;
extern volatile u32 g_nodel_done;   /* 1 quand Node-L a fini son bringup et entre en idle */
