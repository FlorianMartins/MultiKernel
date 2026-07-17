/* NEXUS-OS Node-W — noyau compat NT : pagination propre, GDT/TSS, IDT+Nt (int 0x2e),
 * chargement PE/COFF, exécution du .exe en ring 3, I/O croisée, heartbeat. */
#include "nodew.h"
#include "nodew_mm.h"
#include "pe.h"
#include "serial.h"
#include "kc/cpu.h"

extern void nodew_gdt_init(u64 kernel_stack_top);
extern void nodew_idt_init(void);
extern void enter_user(u64 entry, u64 user_stack);
extern long kctx_save(u64 *buf);

/* PE incorporé (NODE-W/subsystems/pe_blob.c via .incbin) */
extern u8 nodew_pe_start[];
extern u8 nodew_pe_end[];

volatile struct nodew_result g_nodew;
volatile u64 g_nodew_heartbeat;
volatile u32 g_nodew_done;
volatile u32 g_nodew_terminated;
volatile u32 g_nodew_exit_code;
volatile u32 g_nodew_io_ok;
u64          g_nodew_return_ctx[8];

static u8 w_kstack[16384] __attribute__((aligned(16)));

void node_w_main(void) {
    serial_printf("\n[node-w] === Node-W (NT-compat) kernel boot ===\n");

    u64 cr3 = nodew_mm_activate();
    g_nodew.cr3_switched = true;
    serial_printf("[node-w] own paging active (cr3=0x%lx)\n", cr3);

    nodew_gdt_init((u64)(uintptr_t)&w_kstack[sizeof(w_kstack)]);
    nodew_idt_init();
    serial_printf("[node-w] GDT/TSS + IDT ready (Nt syscalls via int 0x2e)\n");

    u64 img_sz = (u64)(nodew_pe_end - nodew_pe_start);
    serial_printf("[node-w] loading PE image (%lu bytes)\n", img_sz);
    u64 entry = pe_load(nodew_pe_start, img_sz,
                        NODEW_USER_WIN_BASE, NODEW_USER_WIN_BASE + NODEW_USER_WIN_SIZE);
    if (!entry) {
        serial_printf("[node-w] PE load FAILED\n");
    } else {
        g_nodew.pe_loaded = true;
        if (kctx_save(g_nodew_return_ctx) == 0) {
            serial_printf("[node-w] entering ring 3 @0x%lx\n", entry);
            g_nodew.user_ran = true;
            enter_user(entry, NODEW_USER_STACK_TOP);
        }
        /* reprise après NtTerminateProcess */
        g_nodew.terminated = (g_nodew_terminated != 0);
        g_nodew.exit_code  = g_nodew_exit_code;
        g_nodew.io_ok      = (g_nodew_io_ok != 0);
        serial_printf("[node-w] back in kernel after NtTerminateProcess (code=%u)\n",
                      g_nodew_exit_code);
    }

    serial_printf("[node-w] ASSERT own-paging: %s\n", g_nodew.cr3_switched ? "PASS" : "FAIL");
    serial_printf("[node-w] ASSERT PE loaded: %s\n", g_nodew.pe_loaded ? "PASS" : "FAIL");
    serial_printf("[node-w] ASSERT ring3 PE ran+terminated: %s\n",
                  (g_nodew.user_ran && g_nodew.terminated) ? "PASS" : "FAIL");
    serial_printf("[node-w] ASSERT cross-node I/O round-trip: %s\n",
                  g_nodew.io_ok ? "PASS" : "FAIL");

    for (u32 i = 0; i < 1000; i++) { g_nodew_heartbeat++; cpu_relax(); }
    g_nodew.heartbeat = g_nodew_heartbeat;
    serial_printf("[node-w] heartbeat=%lu, entering idle\n", g_nodew_heartbeat);

    __atomic_store_n(&g_nodew_done, 1, __ATOMIC_RELEASE);
    for (;;) { g_nodew_heartbeat++; cpu_relax(); }
}
