/* NEXUS-OS COORDINATOR — parsing de la MADT et énumération des cœurs. */
#include "topology.h"
#include "kc/string.h"
#include "serial.h"

void topology_parse_madt(const struct acpi_madt *madt, struct topology *out) {
    memset(out, 0, sizeof(*out));
    if (!madt) return;

    out->local_apic_addr = madt->local_apic_addr;

    const u8 *p   = (const u8 *)madt + sizeof(struct acpi_madt);
    const u8 *end = (const u8 *)madt + madt->header.length;

    while (p + sizeof(struct acpi_madt_entry_hdr) <= end) {
        const struct acpi_madt_entry_hdr *h = (const struct acpi_madt_entry_hdr *)p;
        if (h->length < sizeof(struct acpi_madt_entry_hdr)) break; /* malformé */
        if (p + h->length > end) break;

        if (out->cpu_count < TOPO_MAX_CPUS) {
            if (h->type == ACPI_MADT_LAPIC) {
                const struct acpi_madt_lapic *e = (const struct acpi_madt_lapic *)p;
                struct cpu_desc *c = &out->cpus[out->cpu_count++];
                c->apic_id        = e->apic_id;
                c->enabled        = (e->flags & ACPI_LAPIC_ENABLED) != 0;
                c->online_capable = (e->flags & ACPI_LAPIC_ONLINE_CAPABLE) != 0;
                c->x2apic         = false;
                if (c->enabled) out->enabled_count++;
            } else if (h->type == ACPI_MADT_X2APIC) {
                const struct acpi_madt_x2apic *e = (const struct acpi_madt_x2apic *)p;
                struct cpu_desc *c = &out->cpus[out->cpu_count++];
                c->apic_id        = e->x2apic_id;
                c->enabled        = (e->flags & ACPI_LAPIC_ENABLED) != 0;
                c->online_capable = (e->flags & ACPI_LAPIC_ONLINE_CAPABLE) != 0;
                c->x2apic         = true;
                if (c->enabled) out->enabled_count++;
            }
        }
        p += h->length;
    }
}

void topology_print(const struct topology *t) {
    serial_printf("[topo] Local APIC base : 0x%08x\n", (u32)t->local_apic_addr);
    serial_printf("[topo] CPU entries     : %u (enabled: %u)\n",
                  t->cpu_count, t->enabled_count);
    for (u32 i = 0; i < t->cpu_count; i++) {
        const struct cpu_desc *c = &t->cpus[i];
        serial_printf("       cpu[%u] %s id=%u  %s%s\n",
                      i,
                      c->x2apic ? "x2APIC" : "xAPIC ",
                      c->apic_id,
                      c->enabled ? "ENABLED" : "disabled",
                      c->online_capable ? " (online-capable)" : "");
    }
}
