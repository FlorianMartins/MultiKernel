# SPEC — Phase 7 : Performance & optimisation

> Statut : `IN PROGRESS`. Objectif : faire de MultiKernel/Axis un noyau/OS le plus rapide et
> efficace possible — **par la mesure**, pas par affirmation. On instrumente, on établit
> une baseline en cycles TSC (métrique indépendante de la fréquence), on optimise les
> chemins chauds, on re-mesure, on prouve les gains. Les 6 phases restent vertes.

## 1. Instrumentation (COORDINATOR/bench + userland)

Micro-bench TSC (`rdtsc`), exécutés au boot sur le BSP (déterministe, mono-cœur) :
- **ring SPSC** : N push+pop → cycles/op (cœur du multikernel).
- **memcpy / memset** : cycles/Kio (primitive chaude : IPC, loaders, mm).
- **syscall** (userland) : N × getpid → cycles/syscall (aller-retour ring3↔ring0).
Métrique : **cycles/op** (frequency-independent) — comparable avant/après.

## 2. Optimisations appliquées

1. **memcpy/memset** : copie par mots 64 bits + `rep movsb` (ERMS) au lieu de byte-à-byte.
2. **ring SPSC** : indices **cachés** (le producteur ne relit `consumer_tail` que quand
   il croit plein ; le consommateur cache `producer_head`) → réduit le trafic de cohérence
   MESI inter-cœurs (technique LMAX Disruptor). + drain par lots côté consommateur.
3. **build** : `-O3 -funroll-loops -mtune=generic` ; **LTO** si le noyau freestanding le
   supporte (sinon fallback documenté). Hot paths (`ring_push/pop`) inlinables.
4. **délais** : `udelay` calibré par TSC (au lieu d'une boucle `outb` imprécise).

## 3. Hors scope (documenté, restes)

- **SSE/AVX** : nécessite CR4.OSFXSR + sauvegarde d'état FPU au context-switch → gain
  vectoriel réel mais chantier séparé (noté). On reste `-mgeneral-regs-only` (sûr).
- W^X complet du TCB, IOMMU DMA réelle : cf. Phases précédentes / matériel.

## 4. Résultats mesurés (matériel réel, `make bench-host`, A/B contrôlé)

| Métrique (ring SPSC cross-cœur) | Avant | Après (cached-index) | Gain |
|---|---|---|---|
| Débit | 27.4 M msg/s | **36.1 M msg/s** | **+32 %** |
| Latence consommateur | 138 cycles/msg | **105 cycles/msg** | **−24 %** |

→ Gain réel dû à l'optimisation d'indices cachés (moins de lectures atomiques
cross-cœur / trafic MESI), mesuré à `-O2` des deux côtés (algorithmique, pas codegen).

## 5. Leçon méthodo importante

**QEMU/TCG n'est PAS un banc de perf fiable** : il émule `rep movsb` en boucle
byte-à-byte et n'a pas de cohérence de cache réelle ; les micro-bench kernel sous TCG
sont donc *indicatifs seulement* (et peuvent même inverser le classement). La mesure
autoritaire du ring est le **bench hôte natif** (`make bench-host`, vrai CPU, 2 threads).
Les optimisations conservées sont **correctes pour le matériel réel** : indices cachés,
`memcpy/memset` via `rep movsb/stosb` (ERMS), `-O3 -funroll-loops`. SSE/AVX reste hors
scope (nécessite CR4.OSFXSR + sauvegarde FPU au context-switch) — noté.

## 6. Vérification

- Toutes les phases 1–6 restent vertes (matrice 31/31) + fault-injection (restart, W^X,
  confinement) OK → **aucune régression fonctionnelle** (correction prime sur vitesse).
