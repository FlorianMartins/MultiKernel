/* NEXUS-OS — démo graphique interactive (souris + double buffering). Gated GFX_DEMO. */
#pragma once

#include "acpi.h"

/* Exécutée sur le BSP : IDT + IRQ12 souris + boucle double-bufferisée bornée. */
void gfx_demo_run(const struct acpi_madt *madt);
