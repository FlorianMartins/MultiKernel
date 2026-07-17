/* NEXUS-OS COORDINATOR/smp — réveil des Application Processors & isolation.
 *
 * Le BSP (domaine COORD) : construit un CR3 par domaine, une IDT partagée, copie le
 * trampoline en 0x8000, puis réveille chaque AP (INIT-SIPI-SIPI) un par un. Chaque AP
 * charge le CR3 de son domaine, prouve l'accès à sa propre fenêtre, puis tente un accès
 * à la fenêtre d'un autre domaine -> #PF capturé (isolation prouvée). */
#include "smp.h"
#include "mm.h"
#include "serial.h"
#include "kc/cpu.h"
#include "kc/string.h"

/* ---- symboles fournis par l'assembleur ---- */
extern u64  isr_table[32];
extern void load_idt(void *idt_ptr);
extern u8   tramp_blob_start[];
extern u8   tramp_blob_end[];
void        ap_main(void);          /* appelé depuis le trampoline */
void        exc_handler(u64 vec, u64 err, u64 cr2);

/* ---- physiques fixes du trampoline ---- */
#define TRAMP_ADDR   0x8000ull
#define SIPI_VECTOR  0x08           /* 0x8000 >> 12 */
#define PARAM_CR3    0x9000ull
#define PARAM_STACK  0x9008ull
#define PARAM_ENTRY  0x9010ull
#define PARAM_FLAG   0x9018ull

/* ---- IDT 64 bits ---- */
struct idt_entry {
    u16 off_lo;
    u16 sel;
    u8  ist;
    u8  type_attr;
    u16 off_mid;
    u32 off_hi;
    u32 zero;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

static struct idt_entry g_idt[256] __attribute__((aligned(16)));
static struct idt_ptr   g_idt_ptr;

static void set_gate(int v, u64 handler) {
    g_idt[v].off_lo    = handler & 0xFFFF;
    g_idt[v].sel       = 0x08;              /* code64 de la GDT trampoline (active sur l'AP) */
    g_idt[v].ist       = 0;
    g_idt[v].type_attr = 0x8E;              /* présent, DPL0, interrupt gate */
    g_idt[v].off_mid   = (handler >> 16) & 0xFFFF;
    g_idt[v].off_hi    = (handler >> 32) & 0xFFFFFFFF;
    g_idt[v].zero      = 0;
}

static void idt_init(void) {
    memset(g_idt, 0, sizeof(g_idt));
    for (int i = 0; i < 32; i++) set_gate(i, isr_table[i]);
    g_idt_ptr.limit = sizeof(g_idt) - 1;
    g_idt_ptr.base  = (u64)(uintptr_t)g_idt;
}

/* ---- table runtime des cœurs (partagée, indexée librement) ---- */
struct cpu_rt {
    u32         apic_id;
    enum domain dom;
    u64         win_base;
    u64         forbidden;   /* base de la fenêtre d'un AUTRE domaine */
    bool        used;
};

static struct cpu_rt g_cpus[TOPO_MAX_CPUS];
static u32           g_cpu_n;

static struct cpu_rt *find_cpu(u32 apic_id) {
    for (u32 i = 0; i < g_cpu_n; i++)
        if (g_cpus[i].used && g_cpus[i].apic_id == apic_id)
            return &g_cpus[i];
    return 0;
}

/* ---- compteurs (mis à jour par les AP) ---- */
static volatile u32 g_ap_alive;
static volatile u32 g_iso_pass;
static volatile u32 g_iso_fail;

/* ---- piles par AP (région partagée) ---- */
#define AP_STACK_SIZE 16384
#define AP_MAX        TOPO_MAX_CPUS
static u8 ap_stacks[AP_MAX][AP_STACK_SIZE] __attribute__((aligned(16)));

/* ---- LAPIC (xAPIC MMIO) ---- */
static volatile u8 *g_lapic;
#define LAPIC_SVR  0x0F0
#define LAPIC_ICRL 0x300
#define LAPIC_ICRH 0x310

static u32  lapic_rd(u32 reg)          { return *(volatile u32 *)(g_lapic + reg); }
static void lapic_wr(u32 reg, u32 val) { *(volatile u32 *)(g_lapic + reg) = val; }

static void lapic_wait_idle(void) {
    while (lapic_rd(LAPIC_ICRL) & (1 << 12)) cpu_relax(); /* delivery status */
}

static void lapic_enable(void) {
    lapic_wr(LAPIC_SVR, lapic_rd(LAPIC_SVR) | 0x1FF);     /* enable + vecteur spurious 0xFF */
}

static void lapic_send_init(u32 apic_id) {
    lapic_wr(LAPIC_ICRH, apic_id << 24);
    lapic_wr(LAPIC_ICRL, 0x00004500);                     /* INIT, assert, edge */
    lapic_wait_idle();
}

static void lapic_send_sipi(u32 apic_id, u8 vector) {
    lapic_wr(LAPIC_ICRH, apic_id << 24);
    lapic_wr(LAPIC_ICRL, 0x00004600 | vector);            /* Startup IPI */
    lapic_wait_idle();
}

static void udelay(u32 us) {
    for (u32 i = 0; i < us; i++)
        for (volatile u32 j = 0; j < 40; j++) __asm__ volatile("outb %%al, $0x80" ::: );
}

static bool wait_flag(volatile u32 *f, u64 spins) {
    while (spins--) {
        if (*f) return true;
        cpu_relax();
    }
    return false;
}

/* ================= entrée AP ================= */
void ap_main(void) {
    load_idt(&g_idt_ptr);

    u32 id = cpu_apic_id();
    struct cpu_rt *c = find_cpu(id);
    if (!c) { hlt_forever(); }

    serial_printf("[ap] apic=%u domain=%s  window=[0x%lx..0x%lx)\n",
                  id, mm_domain_name(c->dom),
                  c->win_base, c->win_base + MM_WINDOW_SIZE);

    /* (1) preuve d'accès à SA propre fenêtre RAM */
    volatile u64 *mine = (volatile u64 *)(uintptr_t)(c->win_base + (u64)id * 4096);
    *mine = 0xC0FFEE00ull | id;
    u64 rb = *mine;
    serial_printf("[ap] apic=%u own RAM RW ok @0x%lx = 0x%lx\n",
                  id, (u64)(uintptr_t)mine, rb);
    __atomic_add_fetch(&g_ap_alive, 1, __ATOMIC_SEQ_CST);

    /* (2) accès croisé interdit -> #PF attendu */
    serial_printf("[ap] apic=%u probing forbidden 0x%lx (expect #PF)...\n",
                  id, c->forbidden);
    volatile u64 *bad = (volatile u64 *)(uintptr_t)c->forbidden;
    u64 x = *bad;                    /* déclenche le #PF -> exc_handler (noreturn) */

    /* atteint uniquement si l'isolation a échoué */
    serial_printf("[ap] apic=%u ISOLATION FAIL: read 0x%lx = 0x%lx\n",
                  id, c->forbidden, x);
    __atomic_add_fetch(&g_iso_fail, 1, __ATOMIC_SEQ_CST);
    hlt_forever();
}

void exc_handler(u64 vec, u64 err, u64 cr2) {
    u32 id = cpu_apic_id();
    if (vec == 14) {
        serial_printf("[ap] apic=%u #PF @0x%lx (err=0x%lx) -> ISOLATION OK\n",
                      id, cr2, err);
        __atomic_add_fetch(&g_iso_pass, 1, __ATOMIC_SEQ_CST);
    } else {
        serial_printf("[ap] apic=%u UNEXPECTED EXCEPTION vec=%lu err=0x%lx cr2=0x%lx\n",
                      id, vec, err, cr2);
    }
    hlt_forever();
}

/* ================= BSP : réveil ================= */
struct smp_result smp_boot_aps(const struct topology *t, u64 lapic_base) {
    struct smp_result r;
    memset(&r, 0, sizeof(r));

    g_lapic = (volatile u8 *)(uintptr_t)(lapic_base ? lapic_base : 0xFEE00000ull);

    idt_init();
    lapic_enable();

    u64 cr3_L = mm_build_domain_cr3(DOM_NODE_L);
    u64 cr3_W = mm_build_domain_cr3(DOM_NODE_W);

    /* table runtime des cœurs */
    g_cpu_n = 0;
    for (u32 i = 0; i < t->cpu_count && g_cpu_n < TOPO_MAX_CPUS; i++) {
        enum domain d = mm_domain_of(t, i);
        struct cpu_rt *c = &g_cpus[g_cpu_n++];
        c->apic_id   = t->cpus[i].apic_id;
        c->dom       = d;
        c->win_base  = mm_domain_window_base(d);
        c->forbidden = (d == DOM_NODE_L) ? MM_NODE_W_BASE : MM_NODE_L_BASE;
        c->used      = true;
    }

    /* copie du trampoline en 0x8000 */
    u64 tramp_sz = (u64)(tramp_blob_end - tramp_blob_start);
    memcpy((void *)(uintptr_t)TRAMP_ADDR, tramp_blob_start, tramp_sz);

    volatile u64 *p_cr3   = (volatile u64 *)(uintptr_t)PARAM_CR3;
    volatile u64 *p_stk   = (volatile u64 *)(uintptr_t)PARAM_STACK;
    volatile u64 *p_entry = (volatile u64 *)(uintptr_t)PARAM_ENTRY;
    volatile u32 *p_flag  = (volatile u32 *)(uintptr_t)PARAM_FLAG;

    u32 my = cpu_apic_id();

    serial_printf("\n[smp] --- Phase 2: waking Application Processors ---\n");
    serial_printf("[smp] BSP apic=%u (COORD), trampoline @0x%lx (%lu bytes)\n",
                  my, TRAMP_ADDR, tramp_sz);

    for (u32 i = 0; i < t->cpu_count; i++) {
        enum domain d = mm_domain_of(t, i);
        u32 apic = t->cpus[i].apic_id;
        if (d == DOM_COORD || apic == my) continue;   /* on ne réveille pas le BSP */
        if (!t->cpus[i].enabled) continue;
        r.expected++;

        u64 cr3 = (d == DOM_NODE_L) ? cr3_L : cr3_W;
        u64 stack_top = (u64)(uintptr_t)&ap_stacks[i][AP_STACK_SIZE];

        *p_flag  = 0;
        *p_cr3   = cr3;
        *p_stk   = stack_top;
        *p_entry = (u64)(uintptr_t)&ap_main;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);

        serial_printf("[smp] waking apic=%u -> %s (cr3=0x%lx)\n",
                      apic, mm_domain_name(d), cr3);

        lapic_send_init(apic);
        udelay(200);
        lapic_send_sipi(apic, SIPI_VECTOR);

        if (!wait_flag(p_flag, 2000000)) {
            lapic_send_sipi(apic, SIPI_VECTOR);       /* 2e tentative */
            if (!wait_flag(p_flag, 20000000)) {
                serial_printf("[smp] apic=%u FAILED to start\n", apic);
                continue;
            }
        }
        r.started++;
    }

    /* attend que les AP finissent (accès propre + sonde d'isolation) */
    for (u64 s = 0; s < 50000000 && g_ap_alive < r.expected; s++) cpu_relax();
    for (u64 s = 0; s < 50000000 && (g_iso_pass + g_iso_fail) < r.expected; s++) cpu_relax();

    r.alive    = g_ap_alive;
    r.iso_pass = g_iso_pass;
    r.iso_fail = g_iso_fail;
    return r;
}
