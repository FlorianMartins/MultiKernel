/* NEXUS-OS COORDINATOR/smp — réveil des AP, isolation (Phase 2) & IPC (Phase 3).
 *
 * Le BSP construit un CR3 par domaine, une IDT partagée, réveille les AP
 * (INIT-SIPI-SIPI), et leur attribue un rôle :
 *   - PRODUCER  (1er cœur Node-L)  : pousse N messages dans le ring SHM,
 *   - CONSUMER  (1er cœur Node-W)  : lit/vérifie (ordre+checksum) puis attend un doorbell,
 *   - ISOLATION (autres cœurs)     : sonde la fenêtre d'un autre domaine -> #PF (Phase 2).
 * Le ring vit dans la région partagée [0,32MiB), mappée dans les deux domaines. */
#include "smp.h"
#include "mm.h"
#include "serial.h"
#include "kc/cpu.h"
#include "kc/string.h"
#include "ring.h"
#include "doorbell.h"
#include "io_channel.h"
#include "nodel.h"
#include "nodew.h"

/* ---- symboles asm ---- */
extern u64  isr_table[32];
extern void load_idt(void *idt_ptr);
extern void isr_doorbell(void);
extern u8   tramp_blob_start[];
extern u8   tramp_blob_end[];
void        ap_main(void);
void        exc_handler(u64 vec, u64 err, u64 cr2);

/* ---- physiques fixes du trampoline ---- */
#define TRAMP_ADDR   0x8000ull
#define SIPI_VECTOR  0x08
#define PARAM_CR3    0x9000ull
#define PARAM_STACK  0x9008ull
#define PARAM_ENTRY  0x9010ull
#define PARAM_FLAG   0x9018ull

/* ---- IPC ---- */
#define IPC_MSG_COUNT 100000u

static struct ipc_ring g_ipc __attribute__((aligned(64)));
static volatile u32 g_ipc_producer_done;
static volatile u32 g_ipc_done;
static volatile u32 g_ipc_got;
static volatile u32 g_ipc_order_ok = 1;
static volatile u64 g_ipc_sum;
static volatile u32 g_consumer_waiting;
static volatile u32 g_doorbell_done;
static u32          g_consumer_apic;
volatile u8         g_doorbell_recv;   /* posé par isr_doorbell (asm) */

/* ---- IDT ---- */
struct idt_entry {
    u16 off_lo; u16 sel; u8 ist; u8 type_attr; u16 off_mid; u32 off_hi; u32 zero;
} __attribute__((packed));
struct idt_ptr { u16 limit; u64 base; } __attribute__((packed));

static struct idt_entry g_idt[256] __attribute__((aligned(16)));
static struct idt_ptr   g_idt_ptr;

static void set_gate(int v, u64 handler) {
    g_idt[v].off_lo    = handler & 0xFFFF;
    g_idt[v].sel       = 0x08;              /* code64 de la GDT trampoline */
    g_idt[v].ist       = 0;
    g_idt[v].type_attr = 0x8E;              /* présent, DPL0, interrupt gate */
    g_idt[v].off_mid   = (handler >> 16) & 0xFFFF;
    g_idt[v].off_hi    = (handler >> 32) & 0xFFFFFFFF;
    g_idt[v].zero      = 0;
}

static void idt_init(void) {
    memset(g_idt, 0, sizeof(g_idt));
    for (int i = 0; i < 32; i++) set_gate(i, isr_table[i]);
    set_gate(IPC_DOORBELL_VECTOR, (u64)(uintptr_t)&isr_doorbell);
    g_idt_ptr.limit = sizeof(g_idt) - 1;
    g_idt_ptr.base  = (u64)(uintptr_t)g_idt;
}

/* ---- table runtime des cœurs ---- */
enum ap_role { ROLE_ISOLATION = 0, ROLE_PRODUCER, ROLE_CONSUMER,
               ROLE_NODEL_KERNEL, ROLE_NODEW_KERNEL };

struct cpu_rt {
    u32          apic_id;
    enum domain  dom;
    enum ap_role role;
    u64          win_base;
    u64          forbidden;
    bool         used;
};

static struct cpu_rt g_cpus[TOPO_MAX_CPUS];
static u32           g_cpu_n;

static struct cpu_rt *find_cpu(u32 apic_id) {
    for (u32 i = 0; i < g_cpu_n; i++)
        if (g_cpus[i].used && g_cpus[i].apic_id == apic_id) return &g_cpus[i];
    return 0;
}

/* ---- compteurs isolation ---- */
static volatile u32 g_ap_alive;
static volatile u32 g_iso_pass;
static volatile u32 g_iso_fail;

/* ---- piles par AP ---- */
#define AP_STACK_SIZE 16384
static u8 ap_stacks[TOPO_MAX_CPUS][AP_STACK_SIZE] __attribute__((aligned(16)));

/* ---- LAPIC (BSP) ---- */
static volatile u8 *g_lapic;
#define LAPIC_SVR  0x0F0
#define LAPIC_ICRL 0x300
#define LAPIC_ICRH 0x310

static u32  lapic_rd(u32 reg)          { return *(volatile u32 *)(g_lapic + reg); }
static void lapic_wr(u32 reg, u32 val) { *(volatile u32 *)(g_lapic + reg) = val; }
static void lapic_wait_idle(void)      { while (lapic_rd(LAPIC_ICRL) & (1 << 12)) cpu_relax(); }
static void lapic_enable(void)         { lapic_wr(LAPIC_SVR, lapic_rd(LAPIC_SVR) | 0x1FF); }

static void lapic_send_init(u32 apic_id) {
    lapic_wr(LAPIC_ICRH, apic_id << 24);
    lapic_wr(LAPIC_ICRL, 0x00004500);
    lapic_wait_idle();
}
static void lapic_send_sipi(u32 apic_id, u8 vector) {
    lapic_wr(LAPIC_ICRH, apic_id << 24);
    lapic_wr(LAPIC_ICRL, 0x00004600 | vector);
    lapic_wait_idle();
}
static void udelay(u32 us) {
    for (u32 i = 0; i < us; i++)
        for (volatile u32 j = 0; j < 40; j++) __asm__ volatile("outb %%al, $0x80" ::: );
}
static bool wait_flag(volatile u32 *f, u64 spins) {
    while (spins--) { if (*f) return true; cpu_relax(); }
    return false;
}

/* ================= rôles AP ================= */
static void run_isolation(u32 id, struct cpu_rt *c) {
    serial_printf("[iso] apic=%u domain=%s window=[0x%lx..0x%lx)\n",
                  id, mm_domain_name(c->dom), c->win_base, c->win_base + MM_WINDOW_SIZE);

    volatile u64 *mine = (volatile u64 *)(uintptr_t)(c->win_base + (u64)id * 4096);
    *mine = 0xC0FFEE00ull | id;
    (void)*mine;
    __atomic_add_fetch(&g_ap_alive, 1, __ATOMIC_SEQ_CST);

    serial_printf("[iso] apic=%u probing forbidden 0x%lx (expect #PF)...\n", id, c->forbidden);
    volatile u64 *bad = (volatile u64 *)(uintptr_t)c->forbidden;
    u64 x = *bad;                               /* -> #PF -> exc_handler (noreturn) */

    serial_printf("[iso] apic=%u ISOLATION FAIL: read 0x%lx = 0x%lx\n", id, c->forbidden, x);
    __atomic_add_fetch(&g_iso_fail, 1, __ATOMIC_SEQ_CST);
    hlt_forever();
}

static void run_producer(u32 id) {
    __atomic_add_fetch(&g_ap_alive, 1, __ATOMIC_SEQ_CST);
    serial_printf("[ipc] apic=%u PRODUCER start (%u msgs)\n", id, IPC_MSG_COUNT);

    u32 busy = 0;
    for (u32 seq = 0; seq < IPC_MSG_COUNT; seq++) {
        struct ipc_msg m;
        memset(&m, 0, sizeof(m));
        m.seq = seq;
        m.len = 4;
        memcpy(m.data, &seq, 4);
        while (!ipc_ring_push(&g_ipc, &m)) { busy++; cpu_relax(); }
    }
    __atomic_store_n(&g_ipc_producer_done, 1, __ATOMIC_RELEASE);
    serial_printf("[ipc] apic=%u PRODUCER done, busy-waits=%u\n", id, busy);

    /* doorbell : attendre que le consommateur soit en hlt, puis sonner */
    for (u64 s = 0; s < 500000000ull && !__atomic_load_n(&g_consumer_waiting, __ATOMIC_ACQUIRE); s++)
        cpu_relax();
    for (volatile u32 d = 0; d < 200000; d++) { }   /* marge */
    serial_printf("[ipc] apic=%u ringing doorbell -> apic=%u (vec 0x%x)\n",
                  id, g_consumer_apic, IPC_DOORBELL_VECTOR);
    doorbell_ring(g_consumer_apic, IPC_DOORBELL_VECTOR);
    hlt_forever();
}

static void run_consumer(u32 id) {
    __atomic_add_fetch(&g_ap_alive, 1, __ATOMIC_SEQ_CST);
    serial_printf("[ipc] apic=%u CONSUMER start\n", id);

    u64 sum = 0, empty_budget = 0;
    u32 got = 0, empty = 0, expect = 0, rejected = 0;
    int order = 1;

    while (got < IPC_MSG_COUNT) {
        struct ipc_msg m;
        if (ipc_ring_pop(&g_ipc, &m)) {
            /* SÉCURITÉ (modèle de menace) : tout champ venant de l'AUTRE nœud est
             * hostile par défaut. On borne AVANT toute utilisation — jamais de
             * longueur/pointeur cross-domaine pris au mot. cf. DOCS/security-model.md */
            if (m.len > IPC_PAYLOAD_MAX) { rejected++; continue; }
            if (m.seq != expect) order = 0;
            expect++; sum += m.seq; got++;
            empty_budget = 0;
        } else {
            empty++; cpu_relax();
            if (__atomic_load_n(&g_ipc_producer_done, __ATOMIC_ACQUIRE))
                if (++empty_budget > 500000000ull) break;   /* garde-fou anti-hang si perte */
        }
    }

    g_ipc_got      = got;
    g_ipc_order_ok = order;
    g_ipc_sum      = sum;
    __atomic_store_n(&g_ipc_done, 1, __ATOMIC_RELEASE);

    u64 expect_sum = (u64)(IPC_MSG_COUNT - 1) * (u64)IPC_MSG_COUNT / 2;
    serial_printf("[ipc] apic=%u CONSUMER got=%u order=%s checksum=%s empty-polls=%u rejected=%u\n",
                  id, got, order ? "OK" : "BAD",
                  (sum == expect_sum && got == IPC_MSG_COUNT) ? "OK" : "BAD", empty, rejected);

    /* doorbell : se mettre en attente puis dormir (sti;hlt) jusqu'à l'IPI */
    serial_printf("[ipc] apic=%u waiting for doorbell (sti;hlt)...\n", id);
    __atomic_store_n(&g_consumer_waiting, 1, __ATOMIC_RELEASE);
    for (int i = 0; i < 32 && !g_doorbell_recv; i++)
        __asm__ volatile("sti; hlt");

    if (g_doorbell_recv) {
        serial_printf("[ipc] apic=%u DOORBELL received -> woken from hlt\n", id);
        __atomic_store_n(&g_doorbell_done, 1, __ATOMIC_RELEASE);
    } else {
        serial_printf("[ipc] apic=%u DOORBELL missed\n", id);
    }
    hlt_forever();
}

/* Active le Local APIC de CE cœur (SVR bit 8) — requis pour émettre/recevoir des IPI.
 * La MMIO LAPIC est mappée dans le CR3 du domaine (cf. mm.c). */
static void ap_lapic_enable(void) {
    volatile u32 *svr = (volatile u32 *)(uintptr_t)(0xFEE00000ull + LAPIC_SVR);
    *svr |= 0x1FF;   /* enable + vecteur spurious 0xFF */
}

/* ================= entrée AP ================= */
void ap_main(void) {
    load_idt(&g_idt_ptr);
    ap_lapic_enable();
    u32 id = cpu_apic_id();
    struct cpu_rt *c = find_cpu(id);
    if (!c) hlt_forever();

    switch (c->role) {
    case ROLE_PRODUCER:     run_producer(id);      break;
    case ROLE_CONSUMER:     run_consumer(id);      break;
    case ROLE_NODEL_KERNEL:
        __atomic_add_fetch(&g_ap_alive, 1, __ATOMIC_SEQ_CST);
        serial_printf("[smp] apic=%u -> Node-L sovereign kernel\n", id);
        node_l_main();                             /* ne revient pas (idle) */
        break;
    case ROLE_NODEW_KERNEL:
        __atomic_add_fetch(&g_ap_alive, 1, __ATOMIC_SEQ_CST);
        serial_printf("[smp] apic=%u -> Node-W sovereign kernel\n", id);
        node_w_main();                             /* ne revient pas (idle) */
        break;
    default:                run_isolation(id, c);  break;
    }
}

void exc_handler(u64 vec, u64 err, u64 cr2) {
    u32 id = cpu_apic_id();
    if (vec == 14) {
        serial_printf("[iso] apic=%u #PF @0x%lx (err=0x%lx) -> ISOLATION OK\n", id, cr2, err);
        __atomic_add_fetch(&g_iso_pass, 1, __ATOMIC_SEQ_CST);
    } else {
        serial_printf("[ap] apic=%u UNEXPECTED EXCEPTION vec=%lu err=0x%lx cr2=0x%lx\n",
                      id, vec, err, cr2);
    }
    hlt_forever();
}

/* ================= BSP : réveil + orchestration ================= */
struct smp_result smp_boot_aps(const struct topology *t, u64 lapic_base) {
    struct smp_result r;
    memset(&r, 0, sizeof(r));

    g_lapic = (volatile u8 *)(uintptr_t)(lapic_base ? lapic_base : 0xFEE00000ull);

    idt_init();
    lapic_enable();
    ipc_ring_init(&g_ipc);
    io_channel_init();               /* canal I/O croisé Node-W <-> Node-L (Phase 5) */

    u64 cr3_L = mm_build_domain_cr3(DOM_NODE_L);
    u64 cr3_W = mm_build_domain_cr3(DOM_NODE_W);
    u32 my = cpu_apic_id();

    /* Rôles : 1er cœur Node-L -> NODEL_KERNEL, 2e -> PRODUCER (IPC) ;
     *         1er cœur Node-W -> NODEW_KERNEL, 2e -> CONSUMER (IPC).
     * La démo IPC (Phase 3) ne tourne que s'il y a un 2e cœur de chaque nœud. */
    int nodel_idx = -1, prod_idx = -1, nodew_idx = -1, cons_idx = -1;
    for (u32 i = 0; i < t->cpu_count; i++) {
        enum domain d = mm_domain_of(t, i);
        if (t->cpus[i].apic_id == my || d == DOM_COORD || !t->cpus[i].enabled) continue;
        if (d == DOM_NODE_L) {
            if (nodel_idx < 0)      nodel_idx = (int)i;
            else if (prod_idx < 0)  prod_idx  = (int)i;
        }
        if (d == DOM_NODE_W) {
            if (nodew_idx < 0)      nodew_idx = (int)i;
            else if (cons_idx < 0)  cons_idx  = (int)i;
        }
    }
    bool ipc_on = (prod_idx >= 0 && cons_idx >= 0);

    /* Construire la table runtime + attribuer les rôles. */
    g_cpu_n = 0;
    for (u32 i = 0; i < t->cpu_count && g_cpu_n < TOPO_MAX_CPUS; i++) {
        enum domain d = mm_domain_of(t, i);
        struct cpu_rt *c = &g_cpus[g_cpu_n++];
        c->apic_id   = t->cpus[i].apic_id;
        c->dom       = d;
        c->win_base  = mm_domain_window_base(d);
        c->forbidden = (d == DOM_NODE_L) ? MM_NODE_W_BASE : MM_NODE_L_BASE;
        c->used      = true;
        c->role      = ROLE_ISOLATION;
        if ((int)i == nodel_idx)          c->role = ROLE_NODEL_KERNEL;
        if ((int)i == nodew_idx)          c->role = ROLE_NODEW_KERNEL;
        if (ipc_on && (int)i == prod_idx) c->role = ROLE_PRODUCER;
        if (ipc_on && (int)i == cons_idx) c->role = ROLE_CONSUMER;
    }
    if (ipc_on) {
        g_consumer_apic = t->cpus[cons_idx].apic_id;
        r.ipc_enabled   = true;
        r.ipc_expected  = IPC_MSG_COUNT;
    }
    r.nodel_present = (nodel_idx >= 0);
    r.nodew_present = (nodew_idx >= 0);

    /* Copier le trampoline. */
    u64 tramp_sz = (u64)(tramp_blob_end - tramp_blob_start);
    memcpy((void *)(uintptr_t)TRAMP_ADDR, tramp_blob_start, tramp_sz);

    volatile u64 *p_cr3   = (volatile u64 *)(uintptr_t)PARAM_CR3;
    volatile u64 *p_stk   = (volatile u64 *)(uintptr_t)PARAM_STACK;
    volatile u64 *p_entry = (volatile u64 *)(uintptr_t)PARAM_ENTRY;
    volatile u32 *p_flag  = (volatile u32 *)(uintptr_t)PARAM_FLAG;

    serial_printf("\n[smp] --- Phase 2/3: waking Application Processors ---\n");
    serial_printf("[smp] BSP apic=%u (COORD), trampoline @0x%lx (%lu bytes), ipc=%s\n",
                  my, TRAMP_ADDR, tramp_sz, ipc_on ? "on" : "off");

    for (u32 i = 0; i < t->cpu_count; i++) {
        enum domain d = mm_domain_of(t, i);
        u32 apic = t->cpus[i].apic_id;
        if (d == DOM_COORD || apic == my || !t->cpus[i].enabled) continue;
        r.expected++;
        if (g_cpus[i].role == ROLE_ISOLATION) r.iso_expected++;

        u64 cr3 = (d == DOM_NODE_L) ? cr3_L : cr3_W;

        *p_flag  = 0;
        *p_cr3   = cr3;
        *p_stk   = (u64)(uintptr_t)&ap_stacks[i][AP_STACK_SIZE];
        *p_entry = (u64)(uintptr_t)&ap_main;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);

        const char *role = g_cpus[i].role == ROLE_PRODUCER     ? "PRODUCER"
                         : g_cpus[i].role == ROLE_CONSUMER     ? "CONSUMER"
                         : g_cpus[i].role == ROLE_NODEL_KERNEL ? "NODE-L-KERNEL"
                         : g_cpus[i].role == ROLE_NODEW_KERNEL ? "NODE-W-KERNEL" : "isolation";
        serial_printf("[smp] waking apic=%u -> %s [%s] (cr3=0x%lx)\n",
                      apic, mm_domain_name(d), role, cr3);

        lapic_send_init(apic);
        udelay(200);
        lapic_send_sipi(apic, SIPI_VECTOR);
        if (!wait_flag(p_flag, 2000000)) {
            lapic_send_sipi(apic, SIPI_VECTOR);
            if (!wait_flag(p_flag, 20000000)) {
                serial_printf("[smp] apic=%u FAILED to start\n", apic);
                continue;
            }
        }
        r.started++;
    }

    /* Attendre isolation + IPC (bornés : jamais de hang infini). */
    for (u64 s = 0; s < 100000000ull && g_ap_alive < r.expected; s++) cpu_relax();
    for (u64 s = 0; s < 100000000ull && (g_iso_pass + g_iso_fail) < r.iso_expected; s++) cpu_relax();
    if (ipc_on) {
        for (u64 s = 0; s < 300000000ull && !g_ipc_done; s++) cpu_relax();
        for (u64 s = 0; s < 100000000ull && !g_doorbell_done; s++) cpu_relax();
    }

    /* Node-L : attendre que le noyau Node-L ait fini d'imprimer tout son bringup
     * (flag g_nodel_done) AVANT de terminer QEMU — sinon on tronque sa sortie.
     * Borne en temps réel via le TSC (indépendant de la charge hôte) ; le vrai
     * garde-fou anti-hang reste le timeout externe (run_phaseN.sh). */
    if (r.nodel_present) {
        u64 deadline = rdtsc() + 15000000000ull;     /* ~10-30 s selon la fréquence TSC */
        while (!g_nodel_done && rdtsc() < deadline) cpu_relax();
        u64 hb0 = g_nodel_heartbeat;                 /* puis vérifier le heartbeat vivant */
        u64 d2 = rdtsc() + 2000000000ull;
        while (g_nodel_heartbeat == hb0 && rdtsc() < d2) cpu_relax();
        r.nodel_heartbeat = g_nodel_heartbeat;
        r.nodel_alive     = g_nodel_done && (g_nodel_heartbeat > hb0);
    }

    /* Node-W : idem (attendre son bringup complet avant de terminer QEMU). */
    if (r.nodew_present) {
        u64 deadline = rdtsc() + 15000000000ull;
        while (!g_nodew_done && rdtsc() < deadline) cpu_relax();
        u64 hb0 = g_nodew_heartbeat;
        u64 d2 = rdtsc() + 2000000000ull;
        while (g_nodew_heartbeat == hb0 && rdtsc() < d2) cpu_relax();
        r.nodew_heartbeat = g_nodew_heartbeat;
        r.nodew_alive     = g_nodew_done && (g_nodew_heartbeat > hb0);
        r.nodew_io_ok     = (g_nodew.io_ok != 0);
        r.nodew_terminated = (g_nodew.terminated != 0);
    }

    r.alive        = g_ap_alive;
    r.iso_pass     = g_iso_pass;
    r.iso_fail     = g_iso_fail;
    r.ipc_got      = g_ipc_got;
    r.ipc_order_ok = (g_ipc_order_ok != 0);
    u64 expect_sum = (u64)(IPC_MSG_COUNT - 1) * (u64)IPC_MSG_COUNT / 2;
    r.ipc_sum_ok   = (g_ipc_sum == expect_sum) && (g_ipc_got == IPC_MSG_COUNT);
    r.doorbell_ok  = (g_doorbell_done != 0);
    return r;
}
