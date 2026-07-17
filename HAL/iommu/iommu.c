/* NEXUS-OS HAL — détection IOMMU (table ACPI DMAR / VT-d).
 * Phase 6 = détection + journalisation. L'isolation DMA réelle (programmation des
 * tables de remapping) et sa preuve sont un test MATÉRIEL (QEMU intel-iommu partiel). */
#include "iommu.h"
#include "kc/string.h"
#include "serial.h"

/* En-tête DMAR : SDT header + host_addr_width(1) + flags(1) + reserved(10), puis
 * une suite d'unités de remapping { type(2), length(2), ... }. Type 0 = DRHD. */
struct dmar_header {
    struct acpi_sdt_header h;
    u8  host_addr_width;
    u8  flags;
    u8  reserved[10];
    u8  units[];
} __attribute__((packed));

#define DMAR_TYPE_DRHD 0
#define DMAR_FLAG_INTR_REMAP (1u << 0)

void iommu_detect(const struct acpi_rsdp *rsdp, struct iommu_info *out) {
    memset(out, 0, sizeof(*out));

    const struct acpi_sdt_header *h = acpi_find_table(rsdp, "DMAR");
    if (!h) {
        serial_printf("[iommu] DMAR absente : pas d'IOMMU annoncée par le firmware\n");
        serial_printf("[iommu] ASSERT DMA isolation: N/A (test matériel requis, cf. C1)\n");
        return;
    }

    const struct dmar_header *d = (const struct dmar_header *)h;
    out->present         = true;
    out->host_addr_width = d->host_addr_width + 1;
    out->intr_remap      = (d->flags & DMAR_FLAG_INTR_REMAP) != 0;

    const u8 *p   = d->units;
    const u8 *end = (const u8 *)d + d->h.length;
    while (p + 4 <= end) {
        u16 type = *(const u16 *)p;
        u16 len  = *(const u16 *)(p + 2);
        if (len < 4 || p + len > end) break;
        if (type == DMAR_TYPE_DRHD) out->drhd_count++;
        p += len;
    }

    serial_printf("[iommu] DMAR trouvée : host-addr-width=%u bits, DRHD=%u, intr-remap=%s\n",
                  out->host_addr_width, out->drhd_count, out->intr_remap ? "oui" : "non");
    serial_printf("[iommu] ASSERT IOMMU detected: PASS (programmation DMA = Phase matériel, C1)\n");
}
