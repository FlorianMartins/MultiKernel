# SPEC — Phase 6 : Résilience, durcissement & matériel réel

> Statut : `IMPLEMENTED (bringup)`. Portée : fault-containment + redémarrage à chaud d'un
> nœud, W^X/NX, détection IOMMU (DMAR), checklist « anti-cheat readiness ». Le volet
> matériel réel (IOMMU DMA, Secure Boot) est cadré mais non prouvable sous QEMU.

## 1. Fault containment + hot restart (COORDINATOR/monitor + panic)

Le Coordinator surveille le heartbeat de chaque nœud. Si un nœud n'a pas signalé
`done` et que son heartbeat **ne progresse plus** dans une fenêtre (bornée TSC), il est
déclaré **en faute** ; le Coordinator :
1. **contient** : journalise, considère le cœur figé (le #PF l'a déjà arrêté) ;
2. **redémarre à chaud** : re-programme les params du trampoline (CR3 du domaine, pile
   fraîche, entrée) et renvoie **INIT-SIPI-SIPI** au cœur du nœud ;
3. **vérifie la reprise** : attend `done` du nœud relancé (borné TSC).
Node-L reste **totalement indemne** pendant l'opération (heartbeat L continu).
Démo déterministe : build `NODEW_FAULT_ONCE=1` → Node-W faute au 1er boot
(`g_nodew_boot_count==1`, écrit dans la RAM Node-L → #PF), puis **réussit au 2e** boot.
Testé par `run_phase6_restart.sh`. En build nominal, le monitor confirme « nœuds sains ».

## 2. W^X / NX (durcissement C4)

`EFER.NXE` activé (BSP `boot.asm` + AP `trampoline.asm`). La fenêtre user des nœuds est
mappée **NX sauf la page 2 MiB contenant le code chargé** (ELF/PE) : la pile et les données
user ne sont pas exécutables. Démo : build `NODEL_WX_TEST=1` → le userland tente d'exécuter
depuis sa pile → **#PF** (protection NX), capturé. Testé par `run_phase6_wx.sh`.
⚠️ Reste ouvert : W^X **complet du TCB** (kernel) exige un remapping fin (4 KiB) de la
basse mémoire — noté C4 partiel.

## 3. Détection IOMMU / DMAR (C1 partiel, HAL/iommu)

Parse la table ACPI **DMAR** (comme la MADT) : présence, nb d'unités de remapping (DRHD).
Journalise. ⚠️ L'isolation DMA **réelle** n'est pas prouvable sous QEMU (intel-iommu
partiel) — c'est un **test matériel** (documenté). Fait passer C1 de « planifié » à
« détecté ».

## 4. Checklist « anti-cheat readiness » (COORDINATOR/monitor)

Journalise l'exposition vue par un anti-cheat : bit **hyperviseur** `CPUID.1:ECX[31]`
(0 attendu sur bare-metal ; **1 sous QEMU** — noté honnêtement), **TSC invariant**
(`CPUID.80000007:EDX[8]`), accès MSR/IO direct du domaine. Objectif : documenter la
posture (accès matériel direct, pas de VM-exit imposé), pas décréter.

## 5. Vérification

- Nominal (`run_phase6.sh`, superset 1–5) : monitor « nœuds sains », checklist + DMAR
  journalisés, tout vert ; matrice inchangée.
- `run_phase6_restart.sh` (NODEW_FAULT_ONCE) : fault détectée → restart → Node-W **recouvré**,
  Node-L jamais perturbé.
- `run_phase6_wx.sh` (NODEL_WX_TEST) : exec depuis pile user → #PF NX.
- `make ci` vert (aucune dépendance croisée).
