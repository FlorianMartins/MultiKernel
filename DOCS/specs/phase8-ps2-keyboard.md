# SPEC — Phase 8 : Pilote clavier PS/2 bufferisé par interruption

> Statut : `IN PROGRESS`. Objectif : un vrai pilote clavier piloté par IRQ (plus de perte
> de caractères sous charge, contrairement au polling série). Fondation pour l'interactif
> réel et la future GUI. Vit côté **Node-L** (le nœud qui a un shell) ; matériel via HAL.

## 1. Chaîne matérielle

Clavier PS/2 → contrôleur **8042** (ports data `0x60`, cmd/status `0x64`) → **IRQ1**.
Routage moderne via **IO-APIC** (cohérent avec notre stack APIC/LAPIC) :
- **PIC 8259 masqué** (`0x21`/`0xA1` = 0xFF) pour éviter la double-délivrance.
- IO-APIC (base lue dans la MADT, type 1) : entrée de redirection **GSI1 → vecteur 0x21**,
  delivery physique, **destination = APIC ID du cœur Node-L**, edge, actif haut, démasquée.
  Les *Interrupt Source Override* (MADT type 2) sont pris en compte (IRQ1 peut être remappé).

## 2. Interruptions en ring 3 (Node-L)

Le shell tourne en **ring 3** : pour recevoir l'IRQ clavier, IF=1 en ring 3.
`enter_user(entry, stack, rflags)` reçoit désormais les RFLAGS : **Node-L passe 0x202
(IF=1)**, Node-W garde 0x002 (IF=0, pas d'IRQ). Une IRQ en ring 3 bascule en ring 0 via
**TSS.RSP0** (déjà configuré Phase 4), exécute le handler (gate d'interruption → IF=0,
pas de ré-entrance), EOI **LAPIC**, `iretq`. Seul le cœur Node-L a IF=1 et est ciblé.

## 3. Pilote (`HAL/input/ps2`)

- `ps2_kbd_init()` : vide le buffer de sortie, active le clavier.
- ISR `kbd_isr` (asm) → `kbd_handle()` (C) : lit `0x60`, traduit **scancode set 1 → ASCII**
  (gestion Shift ; les *release*, bit 7, sont ignorés sauf Shift), pousse dans un **ring
  buffer** clavier (SPSC mono-cœur : producteur=ISR, consommateur=shell ; index 32 bits
  alignés → accès atomiques, même cœur). EOI LAPIC.
- `kbd_getc_nonblock()` : dépile, -1 si vide.

## 4. Intégration syscall

`SYS_read` (Node-L) fusionne **série + clavier** : essaie `serial_getc_nonblock` puis
`kbd_getc_nonblock`. Le banc de test web (entrée série) **et** le clavier PS/2 (QEMU
`-display`, v86, matériel réel) fonctionnent tous les deux.

## 5. Vecteurs IDT (Node-L)

`0x20` timer (ignoré + EOI), `0x21` clavier, `0xFF` spurious (iretq seul). Gates
d'interruption DPL0. `int 0x80` reste un trap gate DPL3.

## 6. Vérification

`run_phase8_kbd.sh` : boot QEMU avec un **monitor**, `sendkey` d'une séquence (h,e,l,p,
ret) → injectée dans le 8042 → IRQ1 → buffer → le shell affiche « help » et l'exécute
(vu sur la série). Prouve la chaîne IRQ complète. + matrice Phases 1–6 verte (le web/série
marche toujours), fault-injection OK, `make ci` vert.
