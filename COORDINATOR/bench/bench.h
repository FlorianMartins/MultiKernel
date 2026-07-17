/* NEXUS-OS COORDINATOR — micro-benchmarks TSC (Phase 7 perf).
 * Métrique : cycles/op (indépendante de la fréquence), comparable avant/après. */
#pragma once

/* Exécuté sur le BSP au boot (mono-cœur, déterministe). Journalise les mesures. */
void bench_run(void);
