# SPEC — Phase 10 : Souris PS/2 (IRQ12) + double buffering

> Statut : `IN PROGRESS`. Objectif : la 2e brique d'entrée (souris → curseur) et
> l'anti-scintillement (double buffering) — prérequis du futur compositeur.

## 1. Souris PS/2 (`HAL/input/ps2mouse`)

Même contrôleur 8042, **port auxiliaire**, **IRQ12**. Init :
1. Activer le port aux (`0xA8` → 0x64).
2. Config byte (`0x20` lire / `0x60` écrire) : bit1=1 (IRQ12), bit5=0 (horloge aux).
3. Commandes souris (préfixe `0xD4` puis data 0x60, ACK 0xFA) : `0xF6` (defaults),
   `0xF4` (enable reporting).
Paquets **3 octets** : b0 = boutons(L/R/M) + signes/overflow (bit3=1 toujours),
b1 = dX, b2 = dY (9 bits signés). Curseur : `x += dX`, `y -= dY` (Y inversé), **clampé**
à l'écran. Resync sur le bit3 de b0. État global `{x, y, buttons, packets}`.
IRQ12 → GSI12 (IO-APIC) → vecteur **0x22** → ISR → décode → EOI LAPIC.

## 2. Double buffering (`HAL/gpu/fb`)

Back buffer RAM (1024×768×4 ≈ 3 Mio, statique dans [0,32 Mio)). `fb_enable_backbuffer()`
redirige tous les dessins vers le back buffer ; `fb_present()` copie back→front (le vrai
framebuffer) en un bloc → pas de déchirure/scintillement pendant le redraw.

## 3. Démo interactive (gated `GFX_DEMO=1`)

Sur le **BSP** (identité 4 GiB → FB + LAPIC mappés) : IDT avec gate souris, route IRQ12
vers le BSP, init souris, `sti`, boucle bornée (TSC) : chaque frame dessine un fond + le
**curseur** à la position souris **dans le back buffer**, puis `fb_present()`. Log
périodique de la position. Puis fin → boot continue normalement. En build nominal :
désactivée (boot/matrice inchangés), comme les autres tests de capacité gated.

## 4. Vérification

`run_phase10_mouse.sh` (build `GFX_DEMO=1`) : boot `-vga std` + monitor QEMU,
`mouse_move dx dy` injecte du mouvement → série : paquets reçus > 0 et position curseur
**changée** ; screendump → présence du curseur (couleur distincte). Non-régression :
build nominal → matrice Phases 1–6 + kbd + fb + web verts.
