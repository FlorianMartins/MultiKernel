# SPEC — Phase 1 : Boot Multiboot2 & découverte topologie

> Statut : `IMPLEMENTED (bringup)` — spec de référence du code de la Phase 1.
> Portée : booter le Coordinator sur le seul BSP, parser l'ACPI, énumérer les cœurs via la MADT, imprimer le plan de partitionnement et la carte RAM. **Aucun AP réveillé** (c'est la Phase 2).

## 1. Contrat de handoff GRUB → Coordinator

- Chargement via **GRUB2 / Multiboot2** (`multiboot2 /boot/nexus.elf`).
- À l'entrée `_start` (32-bit protected mode, paging OFF, A20 ON) :
  - `EAX` = `0x36D76289` (magic bootloader Multiboot2).
  - `EBX` = adresse physique de la structure d'information Multiboot2 (MBI).
- Le stub `BOOT/stage2/boot.asm` doit :
  1. sauver `EAX`→`EDI`, `EBX`→`ESI` (futurs args System V `kmain(rdi, rsi)`),
  2. vérifier magic, `CPUID`, support long mode (`CPUID.80000001h:EDX[29]`),
  3. construire une pagination **identité 4 GiB** (2 MiB huge pages : 1×PML4, 1×PDPT, 4×PD),
  4. activer PAE (`CR4.PAE`), `EFER.LME`, `CR0.PG`,
  5. charger la GDT 64-bit, `jmp CS64:long_mode_start`,
  6. recharger les sélecteurs de données, appeler `kmain(magic, mbi)`.

## 2. Source de l'ACPI

Le RSDP est récupéré **depuis les tags Multiboot2** (le firmware l'a déjà localisé) :
- tag type **15** (`ACPI new` / RSDP v2) prioritaire, sinon tag type **14** (`ACPI old` / v1).
- Validation : signature `"RSD PTR "`, checksum v1 (20 o) et checksum étendu (v2, `length` o).
- Résolution table : XSDT (si rev ≥ 2 et `xsdt_addr` ≠ 0) sinon RSDT → recherche de la table de signature `"APIC"` (MADT).

## 3. Énumération des cœurs (MADT)

Parcours des entrées MADT :
- type **0** (Processor Local APIC) : `apic_id`, `flags` (bit0 = *enabled*, bit1 = *online-capable*).
- type **9** (Processor Local x2APIC) : `x2apic_id`, `flags` idem.
Un cœur est compté *utilisable* si `enabled`. La liste `(apic_id, enabled, online_capable, x2apic)` est stockée dans `struct topology`.

## 4. Sorties attendues (console série COM1, 38400 8N1)

- `[acpi] RSDP OK (rev N)`
- `[acpi] MADT found @ 0x…, length …`
- `[topo] CPU entries : K (enabled: E)` + une ligne par cœur.
- `[plan]` : assignation `COORD` (cpu[0]) / `NODE_L` / `NODE_W` (split du reste).
- `[mem]` : carte physique + `ASSERT regions disjoint & sorted: PASS` (invariant Phase 1).

## 5. Critère de validation (QEMU)

`qemu-system-x86_64 -cdrom nexus-os.iso -smp N -m 512 -serial stdio` :
- boot sans triple-fault,
- `enabled == N`,
- assertion de disjonction RAM `PASS`.
Automatisé par `TESTS/qemu/run_phase1.sh` (`make test`). Validé avec N=4 et N=8.

## 6. Prérequis toolchain

`gcc`, `ld`, `nasm`, `make`, `grub-mkrescue`, `xorriso`, `mtools`, `qemu-system-x86`,
et **`grub-pc-bin`** (modules GRUB `i386-pc`). ⚠️ Sans `grub-pc-bin`, `grub-mkrescue`
ne produit qu'une image El Torito **UEFI** que le SeaBIOS par défaut de QEMU ne peut pas
lire (« Could not read from CDROM, code 0009 ») ; l'entrée Multiboot2 32 bits exige le
chemin BIOS/legacy.
