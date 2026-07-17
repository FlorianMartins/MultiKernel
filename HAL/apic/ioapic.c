/* NEXUS-OS HAL — IO-APIC (routage IRQ). Accès indirect via IOREGSEL/IOWIN. */
#include "ioapic.h"
#include "kc/io.h"
#include "kc/string.h"
#include "serial.h"

#define IOREGSEL 0x00
#define IOWIN    0x10
#define IOAPIC_REDTBL 0x10   /* index de la 1re entrée de redirection */

void pic_disable(void) {
    outb(0x21, 0xFF);   /* masque tout sur le PIC maître */
    outb(0xA1, 0xFF);   /* et sur l'esclave */
}

static void ioapic_write(u64 base, u32 reg, u32 val) {
    *(volatile u32 *)(uintptr_t)(base + IOREGSEL) = reg;
    *(volatile u32 *)(uintptr_t)(base + IOWIN) = val;
}

void ioapic_init_from_madt(const struct acpi_madt *madt, u8 isa_irq, struct ioapic_cfg *out) {
    memset(out, 0, sizeof(*out));
    out->kbd_gsi = isa_irq;   /* défaut : identity mapping IRQ->GSI */
    if (!madt) return;

    const u8 *p   = (const u8 *)madt + sizeof(struct acpi_madt);
    const u8 *end = (const u8 *)madt + madt->header.length;
    while (p + sizeof(struct acpi_madt_entry_hdr) <= end) {
        const struct acpi_madt_entry_hdr *h = (const struct acpi_madt_entry_hdr *)p;
        if (h->length < sizeof(*h) || p + h->length > end) break;

        if (h->type == ACPI_MADT_IOAPIC && !out->found) {
            const struct acpi_madt_ioapic *io = (const struct acpi_madt_ioapic *)p;
            out->found    = true;
            out->base     = io->ioapic_addr;
            out->gsi_base = io->gsi_base;
        } else if (h->type == ACPI_MADT_ISO) {
            const struct acpi_madt_iso *iso = (const struct acpi_madt_iso *)p;
            if (iso->source_irq == isa_irq) out->kbd_gsi = iso->gsi;   /* remap ISO */
        }
        p += h->length;
    }

    if (out->found)
        serial_printf("[ioapic] base=0x%08x gsi_base=%u  IRQ%u -> GSI%u\n",
                      (u32)out->base, out->gsi_base, isa_irq, out->kbd_gsi);
    else
        serial_printf("[ioapic] aucun IO-APIC dans la MADT\n");
}

void ioapic_route(const struct ioapic_cfg *cfg, u32 gsi, u8 vector, u8 apic_id) {
    if (!cfg->found) return;
    u32 idx = IOAPIC_REDTBL + (gsi - cfg->gsi_base) * 2;
    /* low : vecteur, fixed, physique, edge, actif-haut, démasquée (bit16=0) */
    ioapic_write(cfg->base, idx,     (u32)vector);
    /* high : destination APIC ID en bits 56..63 (= bits 24..31 du mot haut) */
    ioapic_write(cfg->base, idx + 1, (u32)apic_id << 24);
    serial_printf("[ioapic] GSI%u -> vec 0x%x, dest apic=%u (unmasked)\n", gsi, vector, apic_id);
}
