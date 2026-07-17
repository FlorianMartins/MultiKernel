/* NEXUS-OS Node-L — noyau souverain : pagination propre, GDT/TSS, IDT+syscalls,
 * ordonnanceur coopératif, chargement ELF, lancement du userland en ring 3, heartbeat. */
#include "nodel.h"
#include "nodel_mm.h"
#include "sched.h"
#include "elf.h"
#include "serial.h"
#include "kc/cpu.h"
#include "io_channel.h"   /* back-end I/O croisé servi par Node-L (Phase 5) */
#include "ps2.h"          /* clavier PS/2 bufferisé par interruption (Phase 8) */
#include "ps2mouse.h"     /* souris PS/2 pour le compositeur (Phase 11) */
#include "fb.h"

extern void nodel_gdt_init(u64 kernel_stack_top);
extern void nodel_idt_init(void);
extern void enter_user(u64 entry, u64 user_stack, u64 rflags);
extern long kctx_save(u64 *buf);

/* userland ELF incorporé (LOADERS/elf via .incbin dans user_blob.c) */
extern u8 nodel_user_start[];
extern u8 nodel_user_end[];

/* état partagé (lu par le BSP) */
volatile struct nodel_result g_nodel;
volatile u64 g_nodel_heartbeat;
volatile u32 g_nodel_done;
volatile u32 g_nodel_user_exited;
volatile u32 g_nodel_user_exit_code;
u64          g_return_ctx[8];         /* point de reprise noyau après SYS_exit */

/* pile ring0 pour l'entrée syscall */
static u8 kstack[16384] __attribute__((aligned(16)));

/* --- démo ordonnanceur : 2 tâches coopératives --- */
static volatile u32 task_done;

static void task_a(void) {
    for (int i = 0; i < 3; i++) {
        serial_printf("[node-l] task A tick %d\n", i);
        sched_yield();
    }
    __atomic_add_fetch(&task_done, 1, __ATOMIC_SEQ_CST);
}
static void task_b(void) {
    for (int i = 0; i < 3; i++) {
        serial_printf("[node-l] task B tick %d\n", i);
        sched_yield();
    }
    __atomic_add_fetch(&task_done, 1, __ATOMIC_SEQ_CST);
}

void node_l_main(void) {
    serial_printf("\n[node-l] === Node-L sovereign kernel boot ===\n");

    /* 1) pagination propre + bascule CR3 */
    u64 cr3 = nodel_mm_activate();
    g_nodel.cr3_switched = true;
    serial_printf("[node-l] own paging active (cr3=0x%lx)\n", cr3);

    /* 2) GDT/TSS + IDT (syscalls) */
    nodel_gdt_init((u64)(uintptr_t)&kstack[sizeof(kstack)]);
    nodel_idt_init();
    ps2_kbd_init();   /* le routage IO-APIC de l'IRQ1 -> ce cœur est fait par le BSP */
#ifdef NODEL_GUI
    if (fb_ready()) ps2_mouse_init(fb_get()->width, fb_get()->height);
#endif
    serial_printf("[node-l] GDT/TSS + IDT ready (syscalls int 0x80, clavier PS/2 IRQ)\n");

    /* 3) ordonnanceur : 2 tâches coopératives */
    sched_init();
    sched_spawn(task_a);
    sched_spawn(task_b);
    sched_run();
    g_nodel.tasks_ran = task_done;
    serial_printf("[node-l] scheduler: %u cooperative tasks completed\n", task_done);

    /* 4) charge le userland ELF dans la fenêtre user */
    u64 img_sz = (u64)(nodel_user_end - nodel_user_start);
    serial_printf("[node-l] loading userland ELF (%lu bytes)\n", img_sz);
    u64 entry = elf_load(nodel_user_start, img_sz,
                         NODEL_USER_WIN_BASE, NODEL_USER_WIN_BASE + NODEL_USER_WIN_SIZE);
    if (!entry) {
        serial_printf("[node-l] ELF load FAILED\n");
    } else {
        /* 5) point de reprise puis passage en ring 3 */
        if (kctx_save(g_return_ctx) == 0) {
            serial_printf("[node-l] entering ring 3 @0x%lx (IF=1, clavier actif)\n", entry);
            g_nodel.user_ran = true;
            enter_user(entry, NODEL_USER_STACK_TOP, 0x202);   /* IF=1 : IRQ clavier en ring 3 */
            /* enter_user ne revient pas : le retour se fait par kctx_restore (SYS_exit) */
        }
        /* reprise ici après SYS_exit */
        g_nodel.user_exited    = (g_nodel_user_exited != 0);
        g_nodel.user_exit_code = g_nodel_user_exit_code;
        serial_printf("[node-l] back in kernel after user exit (code=%u)\n",
                      g_nodel_user_exit_code);
    }

    /* 6) heartbeat : Node-L vivant, observé par le Coordinator */
    serial_printf("[node-l] ASSERT own-paging: %s\n", g_nodel.cr3_switched ? "PASS" : "FAIL");
    serial_printf("[node-l] ASSERT scheduler (2 tasks): %s\n",
                  g_nodel.tasks_ran == 2 ? "PASS" : "FAIL");
    serial_printf("[node-l] ASSERT ring3 userland: %s\n",
                  (g_nodel.user_ran && g_nodel.user_exited) ? "PASS" : "FAIL");
    serial_printf("[node-l] ASSERT user exit code 0: %s\n",
                  g_nodel.user_exit_code == 0 ? "PASS" : "FAIL");

    for (u32 i = 0; i < 1000; i++) {
        g_nodel_heartbeat++;
        cpu_relax();
    }
    g_nodel.heartbeat = g_nodel_heartbeat;
    serial_printf("[node-l] heartbeat=%lu, entering idle\n", g_nodel_heartbeat);

    /* Signale au Coordinator que tout le bringup Node-L est imprimé : le BSP peut
     * alors observer le heartbeat et terminer sans tronquer notre sortie. */
    __atomic_store_n(&g_nodel_done, 1, __ATOMIC_RELEASE);

    /* Idle : bat le heartbeat ET sert le back-end I/O croisé pour Node-W (Phase 5). */
    for (;;) {
        g_nodel_heartbeat++;
        io_backend_poll();
        cpu_relax();
    }
}
