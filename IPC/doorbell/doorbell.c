/* NEXUS-OS IPC/doorbell — envoi d'IPI via le Local APIC (xAPIC MMIO).
 * La MMIO LAPIC est mappée (UC) dans chaque CR3 domaine (cf. mm.c). */
#include "doorbell.h"

#define LAPIC_PHYS 0xFEE00000ull
#define LAPIC_ICRL 0x300
#define LAPIC_ICRH 0x310

static volatile u32 *lapic_reg(u32 off) {
    return (volatile u32 *)(uintptr_t)(LAPIC_PHYS + off);
}

void doorbell_ring(u32 apic_id, u8 vector) {
    *lapic_reg(LAPIC_ICRH) = apic_id << 24;               /* destination */
    *lapic_reg(LAPIC_ICRL) = 0x4000u | vector;            /* fixed, assert, edge */
    while (*lapic_reg(LAPIC_ICRL) & (1u << 12))           /* delivery status */
        __asm__ volatile("pause");
}
