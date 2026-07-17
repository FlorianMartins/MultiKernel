/* NEXUS-OS COORDINATOR — monitor : checklist « anti-cheat readiness ». */
#pragma once

/* Journalise la posture d'exposition matérielle vue par un anti-cheat
 * (bit hyperviseur, TSC invariant, accès direct). Ne décrète rien : documente. */
void monitor_anticheat_report(void);
