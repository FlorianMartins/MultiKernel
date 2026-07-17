/* NEXUS-OS Node-W — noyau compat NT (Phase 5). */
#pragma once

#include "kc/types.h"

struct nodew_result {
    bool cr3_switched;
    bool pe_loaded;
    bool user_ran;       /* le PE a tourné en ring 3 */
    bool terminated;     /* NtTerminateProcess reçu */
    u32  exit_code;
    bool io_ok;          /* round-trip I/O croisé vérifié */
    u64  heartbeat;
};

void node_w_main(void);

extern volatile struct nodew_result g_nodew;
extern volatile u64 g_nodew_heartbeat;
extern volatile u32 g_nodew_done;    /* 1 quand Node-W a fini son bringup */
