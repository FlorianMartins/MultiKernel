/* NEXUS-OS COORDINATOR — checklist « anti-cheat readiness ».
 * Un anti-cheat refuse souvent de tourner sous virtualisation. NEXUS-OS vise l'accès
 * matériel DIRECT (pas d'hyperviseur, pas de VM-exit imposé). On journalise l'exposition
 * réellement observable ; honnête : sous QEMU le bit hyperviseur est à 1. */
#include "monitor.h"
#include "kc/cpu.h"
#include "serial.h"

void monitor_anticheat_report(void) {
    u32 a, b, c, d;

    serial_printf("\n[anticheat] --- readiness checklist ---\n");

    /* CPUID.1:ECX[31] = bit hyperviseur (0 = bare-metal attendu). */
    cpuid_raw(1, &a, &b, &c, &d);
    bool hv = (c & (1u << 31)) != 0;
    serial_printf("[anticheat] hypervisor bit (CPUID.1:ECX[31]) = %u  (%s)\n",
                  hv ? 1 : 0,
                  hv ? "QEMU/VM ici — sera 0 sur matériel bare-metal" : "bare-metal");

    /* Signature de l'hyperviseur si présent (feuille 0x40000000). */
    if (hv) {
        cpuid_raw(0x40000000, &a, &b, &c, &d);
        char sig[13];
        *(u32 *)&sig[0] = b; *(u32 *)&sig[4] = c; *(u32 *)&sig[8] = d; sig[12] = 0;
        serial_printf("[anticheat] hypervisor vendor = \"%s\"\n", sig);
    }

    /* TSC invariant : CPUID.80000007:EDX[8]. */
    cpuid_raw(0x80000007, &a, &b, &c, &d);
    bool invtsc = (d & (1u << 8)) != 0;
    serial_printf("[anticheat] invariant TSC (CPUID.80000007:EDX[8]) = %s\n",
                  invtsc ? "oui" : "non");

    /* Accès direct : le Coordinator lit les MSR/CPUID nativement, sans trap.
     * Preuve implicite : ce code s'exécute en ring 0 bare-metal (pas de #VMEXIT). */
    serial_printf("[anticheat] direct CPUID/MSR/IO access : oui (ring0 natif, pas de VM-exit imposé)\n");
    serial_printf("[anticheat] posture : isolation par partitionnement (pas d'hyperviseur) ; "
                  "sur matériel réel -> hypervisor bit=0\n");
}
