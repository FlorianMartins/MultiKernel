/* NEXUS-OS HAL — découverte ACPI.
 * Toutes les adresses physiques ACPI sont directement déréférençables : le stub
 * de boot mappe en identité les 4 premiers GiB (les tables ACPI y résident). */
#include "acpi.h"
#include "kc/string.h"

static bool checksum_ok(const void *p, u32 len) {
    const u8 *b = (const u8 *)p;
    u8 sum = 0;
    for (u32 i = 0; i < len; i++) sum = (u8)(sum + b[i]);
    return sum == 0;
}

const struct acpi_rsdp *acpi_validate_rsdp(const void *p) {
    if (!p) return 0;
    const struct acpi_rsdp *r = (const struct acpi_rsdp *)p;
    if (memcmp(r->sig, "RSD PTR ", 8) != 0) return 0;
    if (!checksum_ok(r, 20)) return 0;              /* checksum v1 */
    if (r->revision >= 2 && !checksum_ok(r, r->length)) return 0; /* checksum v2 */
    return r;
}

static const struct acpi_sdt_header *sdt_at(u64 phys) {
    return (const struct acpi_sdt_header *)(uintptr_t)phys;
}

const struct acpi_sdt_header *acpi_find_table(const struct acpi_rsdp *rsdp, const char sig[4]) {
    if (!rsdp) return 0;

    bool use_xsdt = (rsdp->revision >= 2) && (rsdp->xsdt_addr != 0);
    const struct acpi_sdt_header *root =
        sdt_at(use_xsdt ? rsdp->xsdt_addr : (u64)rsdp->rsdt_addr);
    if (!root) return 0;

    u32 entry_size = use_xsdt ? 8 : 4;
    if (root->length < sizeof(struct acpi_sdt_header)) return 0;
    u32 count = (root->length - (u32)sizeof(struct acpi_sdt_header)) / entry_size;
    const u8 *arr = (const u8 *)root + sizeof(struct acpi_sdt_header);

    for (u32 i = 0; i < count; i++) {
        u64 phys;
        if (use_xsdt) {
            u64 v; memcpy(&v, arr + (u64)i * 8, 8); phys = v;
        } else {
            u32 v; memcpy(&v, arr + (u64)i * 4, 4); phys = v;
        }
        const struct acpi_sdt_header *h = sdt_at(phys);
        if (memcmp(h->sig, sig, 4) == 0) return h;
    }
    return 0;
}

const struct acpi_madt *acpi_find_madt(const struct acpi_rsdp *rsdp) {
    return (const struct acpi_madt *)acpi_find_table(rsdp, "APIC");
}
