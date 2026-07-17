/* NEXUS-OS HAL — détection IOMMU via la table ACPI DMAR (VT-d). */
#pragma once

#include "kc/types.h"
#include "acpi.h"

struct iommu_info {
    bool present;      /* table DMAR trouvée */
    u32  drhd_count;   /* nb d'unités de remapping matériel (DRHD) */
    u8   host_addr_width;
    bool intr_remap;   /* interrupt remapping annoncé */
};

/* Détecte et journalise l'IOMMU (parse DMAR). Ne programme rien (Phase 6 = détection). */
void iommu_detect(const struct acpi_rsdp *rsdp, struct iommu_info *out);
