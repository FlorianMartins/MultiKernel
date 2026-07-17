/* NEXUS-OS HAL — IO-APIC : routage des IRQ matérielles vers un vecteur/cœur.
 * Utilisé pour délivrer l'IRQ clavier (GSI de l'IRQ1) au cœur Node-L. */
#pragma once

#include "kc/types.h"
#include "acpi.h"

struct ioapic_cfg {
    bool found;
    u64  base;        /* MMIO base de l'IO-APIC */
    u32  gsi_base;    /* 1er GSI géré par cet IO-APIC */
    u32  kbd_gsi;     /* GSI effectif de l'IRQ1 clavier (après ISO éventuel) */
};

/* Masque les deux PIC 8259 (évite la double-délivrance en mode APIC). */
void pic_disable(void);

/* Parse la MADT : 1er IO-APIC + résolution du GSI de l'IRQ `isa_irq` (ISO). */
void ioapic_init_from_madt(const struct acpi_madt *madt, u8 isa_irq, struct ioapic_cfg *out);

/* Programme la redirection GSI -> (vector, dest apic_id), edge/actif-haut, démasquée. */
void ioapic_route(const struct ioapic_cfg *cfg, u32 gsi, u8 vector, u8 apic_id);
