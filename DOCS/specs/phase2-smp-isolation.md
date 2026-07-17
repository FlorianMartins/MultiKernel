# SPEC — Phase 2 : Réveil des AP & partitionnement effectif

> Statut : `IMPLEMENTED (bringup)`. Portée : réveiller les Application Processors,
> assigner chaque cœur à un domaine, charger un **CR3 par domaine** (chaque AP ne
> voit que sa partition RAM), et **prouver l'isolation** par un #PF croisé capturé.

## 1. Objectif vérifiable

- BSP (domaine COORD) réveille tous les autres cœurs via **INIT–SIPI–SIPI** (LAPIC).
- Chaque AP entre dans un trampoline (real→protégé→long), charge le **CR3 de son
  domaine** (Node-L ou Node-W), puis exécute `ap_main`.
- Chaque AP : (a) lit son APIC ID (`CPUID.1:EBX[31:24]`), (b) écrit/relit *sa* fenêtre
  RAM (preuve d'accès), (c) tente de lire la fenêtre d'un **autre** domaine → **#PF**
  attendu, capturé par l'IDT, compté comme *ISOLATION OK*.
- Invariants finaux : `started == expected`, `isolation_pass == expected`, `isolation_fail == 0`.

## 2. Plan mémoire (identité, pages 2 MiB)

| Région | Plage physique | Mappée dans |
|---|---|---|
| Shared (kernel, piles, tables, trampoline) | `[0, 32 MiB)` | tous les domaines |
| Fenêtre Node-L | `[64 MiB, 96 MiB)` | CR3 Node-L uniquement |
| Fenêtre Node-W | `[128 MiB, 160 MiB)` | CR3 Node-W uniquement |

Chaque CR3 domaine = PML4→PDPT→PD (1 PD, low 1 GiB) mappant *shared ∪ sa fenêtre*.
Le reste (y compris la fenêtre de l'autre domaine et la MMIO LAPIC) est **non présent**
→ tout accès croisé fait #PF. Requiert `-m >= 256` sous QEMU.

## 3. Trampoline SMP

- Blob assemblé à plat (`nasm -f bin`, `ORG 0x8000`), copié à la physique **0x8000**.
- Bloc de paramètres à **0x9000** (patché par le BSP, un AP à la fois) :
  `{ u64 cr3; u64 stack_top; u64 entry; u32 started_flag }`.
- Séquence AP : `cli`→GDT→PE→charge CR3→PAE→EFER.LME→PG→`jmp CODE64:lm64`→
  charge la pile→`started_flag=1`→`call entry (=ap_main)`.
- APIC ID lu via CPUID (aucune MMIO LAPIC nécessaire côté AP → simplifie le CR3).

## 4. Réveil (BSP, LAPIC xAPIC MMIO @ 0xFEE00000)

`send_init(apic)` = ICR `0x00004500` ; `send_sipi(apic, 0x08)` = ICR `0x00004608`
(vecteur 0x08 → page 0x8000). Poll du bit *delivery status* (ICR bit 12). Un AP à la
fois : params → INIT → SIPI → attente `started_flag` (2e SIPI si besoin).

## 5. IDT & capture #PF

IDT 64 bits partagée, vecteurs 0..31 → stubs asm (`isr.asm`) → `exc_handler(vec,err,cr2)`.
Sur `#PF` (vec 14) : log `ISOLATION OK`, incrément atomique `isolation_pass`, `cli;hlt`.
Gates sélecteur `0x08` (code64 de la GDT trampoline, restée active sur l'AP).

## 6. Validation QEMU

`make test` (`TESTS/qemu/run_phase2.sh`, N=4 et N=8) :
- `started == expected`, `isolation_pass == expected`, aucun `ISOLATION FAIL`, aucune
  `EXCEPTION vec` inattendue.
- ⚠️ Limite : MMIO LAPIC accédée via mapping WB (identité 4 GiB du BSP). QEMU le tolère ;
  sur matériel réel il faudra un mapping UC (noté en risque, cf. `ARCHITECTURE.md` §5).
