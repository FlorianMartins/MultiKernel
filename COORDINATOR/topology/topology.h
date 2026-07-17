/* NEXUS-OS COORDINATOR — énumération de la topologie CPU depuis la MADT. */
#pragma once

#include "kc/types.h"
#include "acpi.h"

#define TOPO_MAX_CPUS 256

struct cpu_desc {
    u32  apic_id;
    bool enabled;         /* utilisable dès maintenant */
    bool online_capable;  /* activable à chaud (hot-plug) */
    bool x2apic;          /* décrit via une entrée x2APIC */
};

struct topology {
    u32 cpu_count;        /* nb d'entrées CPU trouvées */
    u32 enabled_count;    /* dont utilisables */
    u64 local_apic_addr;  /* base MMIO du Local APIC */
    struct cpu_desc cpus[TOPO_MAX_CPUS];
};

void topology_parse_madt(const struct acpi_madt *madt, struct topology *out);
void topology_print(const struct topology *t);
