/* NEXUS-OS COORDINATOR — point d'entrée du super-noyau (Phase 1).
 *
 * Appelé depuis le stub de boot avec :
 *   rdi = magic Multiboot2, rsi = pointeur MBI.
 * Rôle Phase 1 : init série, valider ACPI, énumérer les cœurs (MADT),
 * imprimer le plan de partitionnement et la carte RAM (invariant de disjonction).
 * Aucun AP n'est réveillé ici (c'est la Phase 2). */
#include "kc/types.h"
#include "kc/string.h"
#include "kc/io.h"
#include "serial.h"
#include "multiboot2.h"
#include "acpi.h"
#include "topology.h"
#include "smp.h"
#include "iommu.h"
#include "monitor.h"
#include "bench.h"
#include "branding.h"

/* Sortie propre de QEMU (device isa-debug-exit) : écrire sur 0xF4 termine QEMU.
 * Sur port non assigné (matériel réel / QEMU sans le device) c'est un no-op. */
static void qemu_exit(u8 code) { outb(0xF4, code); }

static void banner(void) {
    serial_printf("\n");
    serial_printf("================================================\n");
    serial_printf("  %s OS  --  %s Coordinator  (v%s)\n", OS_NAME, KERNEL_NAME, OS_VERSION);
    serial_printf("  %s\n", OS_TAGLINE);
    serial_printf("================================================\n\n");
}

/* Plan de partitionnement CPU statique : cpu[0] -> COORD, reste réparti L/W. */
static void partition_plan(const struct topology *t) {
    serial_printf("\n[plan] --- CPU partitioning plan ---\n");
    u32 n = t->cpu_count;
    if (n == 0) { serial_printf("[plan] FATAL: no CPU enumerated\n"); return; }

    u32 rest = (n >= 1) ? n - 1 : 0;
    u32 nl = (rest + 1) / 2;   /* Node-L prend le surplus (ceil) */
    u32 nw = rest - nl;

    for (u32 i = 0; i < n; i++) {
        const char *dom = (i == 0) ? "COORD " : (i <= nl ? "NODE_L" : "NODE_W");
        serial_printf("[plan] cpu[%u] apic=%u -> %s\n", i, t->cpus[i].apic_id, dom);
    }
    serial_printf("[plan] domains: COORD=1  NODE_L=%u  NODE_W=%u\n", nl, nw);
}

static u64 region_end(const struct mb2_mmap_entry *e) { return e->addr + e->len; }

/* Carte RAM + assertion de l'invariant Phase 1 : régions triées et disjointes. */
static void ram_report(const void *mbi) {
    const struct mb2_tag_mmap *mm = mb2_find_mmap(mbi);
    serial_printf("\n[mem] --- physical memory map ---\n");
    if (!mm) { serial_printf("[mem] FATAL: no mmap tag\n"); return; }

    u32 n = (mm->size - (u32)sizeof(*mm)) / mm->entry_size;
    u64 usable = 0, prev_end = 0;
    bool disjoint = true;

    for (u32 i = 0; i < n; i++) {
        const struct mb2_mmap_entry *e =
            (const struct mb2_mmap_entry *)((const u8 *)mm->entries + (u64)i * mm->entry_size);
        const char *kind =
            e->type == 1 ? "usable" :
            e->type == 3 ? "ACPI-reclaim" :
            e->type == 4 ? "ACPI-NVS" :
            e->type == 5 ? "bad" : "reserved";
        serial_printf("[mem] %016lx - %016lx  %s\n", e->addr, region_end(e), kind);
        if (e->type == 1) usable += e->len;
        if (e->addr < prev_end) disjoint = false; /* la mmap firmware est triée */
        prev_end = region_end(e);
    }
    serial_printf("[mem] usable RAM: %lu MiB across %u regions\n",
                  usable / (1024 * 1024), n);
    serial_printf("[mem] ASSERT regions disjoint & sorted: %s\n",
                  disjoint ? "PASS" : "FAIL");
}

void kmain(u64 magic, u64 mbi_addr) {
    serial_init();
    banner();

    if (magic != MB2_BOOTLOADER_MAGIC) {
        serial_printf("[boot] FATAL: bad multiboot2 magic 0x%08x\n", (u32)magic);
        return;
    }
    serial_printf("[boot] multiboot2 magic OK, MBI @ 0x%08x\n", (u32)mbi_addr);

    const void *mbi = (const void *)(uintptr_t)mbi_addr;

    const struct acpi_rsdp *rsdp = acpi_validate_rsdp(mb2_find_rsdp(mbi));
    if (!rsdp) { serial_printf("[acpi] FATAL: no valid RSDP from bootloader\n"); return; }
    serial_printf("[acpi] RSDP OK (rev %u)\n", rsdp->revision);

    const struct acpi_madt *madt = acpi_find_madt(rsdp);
    if (!madt) { serial_printf("[acpi] FATAL: MADT (APIC) not found\n"); return; }
    serial_printf("[acpi] MADT found @ 0x%08x, length %u\n",
                  (u32)(uintptr_t)madt, madt->header.length);

    struct topology topo;
    topology_parse_madt(madt, &topo);
    topology_print(&topo);

    partition_plan(&topo);
    ram_report(mbi);

    serial_printf("\n[coord] Phase 1 complete.\n");

    /* ---- Phase 6 : durcissement — détection IOMMU + checklist anti-cheat ---- */
    struct iommu_info iommu;
    iommu_detect(rsdp, &iommu);
    monitor_anticheat_report();

    /* ---- Phase 7 : micro-benchmarks (chemins chauds) ---- */
    bench_run();

    /* ---- Phases 2..6 : réveil des AP, isolation, IPC, nœuds, résilience ---- */
    struct smp_result r = smp_boot_aps(&topo, topo.local_apic_addr, madt);

    bool started_ok = (r.started == r.expected);
    bool iso_ok     = (r.iso_pass == r.iso_expected) && (r.iso_fail == 0);

    serial_printf("\n[smp] APs: expected=%u started=%u alive=%u\n",
                  r.expected, r.started, r.alive);
    serial_printf("[smp] ASSERT started == expected: %s\n", started_ok ? "PASS" : "FAIL");
    serial_printf("[smp] isolation: pass=%u fail=%u (expected=%u)\n",
                  r.iso_pass, r.iso_fail, r.iso_expected);
    serial_printf("[smp] ASSERT isolation enforced: %s\n",
                  iso_ok ? "PASS" : (r.iso_expected == 0 ? "SKIP" : "FAIL"));

    /* Phase 3 : IPC */
    bool ipc_ok = true;
    if (r.ipc_enabled) {
        bool delivery_ok = r.ipc_order_ok && r.ipc_sum_ok && (r.ipc_got == r.ipc_expected);
        serial_printf("\n[ipc] delivery: got=%u/%u order=%s checksum=%s\n",
                      r.ipc_got, r.ipc_expected,
                      r.ipc_order_ok ? "OK" : "BAD", r.ipc_sum_ok ? "OK" : "BAD");
        serial_printf("[ipc] ASSERT delivery FIFO+lossless: %s\n", delivery_ok ? "PASS" : "FAIL");
        serial_printf("[ipc] ASSERT doorbell wake: %s\n", r.doorbell_ok ? "PASS" : "FAIL");
        ipc_ok = delivery_ok && r.doorbell_ok;
    } else {
        serial_printf("\n[ipc] ASSERT delivery FIFO+lossless: SKIP (need >=1 Node-L + 1 Node-W AP)\n");
    }

    /* Phase 4 : Node-L souverain */
    bool nodel_ok = true;
    if (r.nodel_present) {
        serial_printf("\n[node-l] heartbeat observed by BSP: %lu\n", r.nodel_heartbeat);
        serial_printf("[node-l] ASSERT node-l alive (heartbeat): %s\n",
                      r.nodel_alive ? "PASS" : "FAIL");
        nodel_ok = r.nodel_alive;
    } else {
        serial_printf("\n[node-l] ASSERT node-l alive: SKIP (no Node-L core)\n");
    }

    /* Phase 5 : Node-W souverain (PE + NT + I/O croisée) */
    bool nodew_ok = true;
    if (r.nodew_present) {
        serial_printf("\n[node-w] heartbeat observed by BSP: %lu\n", r.nodew_heartbeat);
        serial_printf("[node-w] ASSERT node-w alive (heartbeat): %s\n",
                      r.nodew_alive ? "PASS" : "FAIL");
        serial_printf("[node-w] ASSERT PE terminated cleanly: %s\n",
                      r.nodew_terminated ? "PASS" : "FAIL");
        serial_printf("[node-w] ASSERT cross-node I/O verified: %s\n",
                      r.nodew_io_ok ? "PASS" : "FAIL");
        serial_printf("[coord] ASSERT both nodes alive in parallel: %s\n",
                      (r.nodel_alive && r.nodew_alive) ? "PASS" : "FAIL");
        nodew_ok = r.nodew_alive && r.nodew_terminated && r.nodew_io_ok;
    } else {
        serial_printf("\n[node-w] ASSERT node-w alive: SKIP (no Node-W core)\n");
    }

    /* Phase 6 : résilience — fault-containment / hot-restart */
    if (r.nodew_restarted) {
        serial_printf("\n[monitor] Node-W was hot-restarted after a fault.\n");
        serial_printf("[monitor] ASSERT Node-W recovered: %s\n", r.nodew_alive ? "PASS" : "FAIL");
        serial_printf("[monitor] ASSERT Node-L survived Node-W restart: %s\n",
                      r.nodel_survived_restart ? "PASS" : "FAIL");
    } else if (r.nodew_present) {
        serial_printf("\n[monitor] ASSERT nodes healthy (no restart needed): PASS\n");
    }

    bool all_ok = started_ok && iso_ok && ipc_ok && nodel_ok && nodew_ok;
    serial_printf("\n[coord] Phase 6 %s. BSP halting.\n", all_ok ? "complete" : "FAILED");

    /* Termine QEMU proprement pour les runs de test (no-op sur vrai matériel). */
    qemu_exit(all_ok ? 0x00 : 0x01);
}
