/* NEXUS-OS HAL — structures et découverte ACPI (RSDP / XSDT / RSDT / MADT). */
#pragma once

#include "kc/types.h"

struct acpi_rsdp {
    char sig[8];        /* "RSD PTR " */
    u8   checksum;      /* somme des 20 premiers octets == 0 */
    char oem_id[6];
    u8   revision;      /* 0 = ACPI 1.0 (RSDT) ; >= 2 = ACPI 2.0+ (XSDT) */
    u32  rsdt_addr;
    /* champs v2 : */
    u32  length;
    u64  xsdt_addr;
    u8   ext_checksum;  /* somme des `length` octets == 0 */
    u8   reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
    char sig[4];
    u32  length;
    u8   revision;
    u8   checksum;
    char oem_id[6];
    char oem_table_id[8];
    u32  oem_revision;
    u32  creator_id;
    u32  creator_revision;
} __attribute__((packed));

/* MADT (signature "APIC") */
struct acpi_madt {
    struct acpi_sdt_header header;
    u32 local_apic_addr;
    u32 flags;
    u8  entries[];      /* suite d'entrées { type, length, ... } */
} __attribute__((packed));

struct acpi_madt_entry_hdr {
    u8 type;
    u8 length;
} __attribute__((packed));

#define ACPI_MADT_LAPIC  0  /* Processor Local APIC */
#define ACPI_MADT_X2APIC 9  /* Processor Local x2APIC */

struct acpi_madt_lapic {
    struct acpi_madt_entry_hdr h;
    u8  acpi_processor_id;
    u8  apic_id;
    u32 flags;
} __attribute__((packed));

struct acpi_madt_x2apic {
    struct acpi_madt_entry_hdr h;
    u16 reserved;
    u32 x2apic_id;
    u32 flags;
    u32 acpi_uid;
} __attribute__((packed));

#define ACPI_LAPIC_ENABLED        (1u << 0)
#define ACPI_LAPIC_ONLINE_CAPABLE (1u << 1)

/* Valide un RSDP brut (signature + checksums). Renvoie NULL si invalide. */
const struct acpi_rsdp *acpi_validate_rsdp(const void *rsdp);

/* Localise une table ACPI par signature 4 caractères (ex "APIC", "DMAR"). NULL si absente. */
const struct acpi_sdt_header *acpi_find_table(const struct acpi_rsdp *rsdp, const char sig[4]);

/* Localise la MADT via XSDT (préféré) ou RSDT. Renvoie NULL si absente. */
const struct acpi_madt *acpi_find_madt(const struct acpi_rsdp *rsdp);
