# SPEC — Phase 11 : Compositeur / fenêtres en userland Node-L

> Statut : `IN PROGRESS`. Objectif : un vrai compositeur GUI **en ring 3** (userland
> Node-L) — desktop, fenêtres empilées avec barres de titre, curseur souris, drag &
> drop, z-order — le tout double-bufferisé. Gated `NODEL_GUI` (shell nominal intact).

## 1. Frontière noyau/GUI (sécurité)

Le userland **ne touche jamais la MMIO GPU** directement. Il dessine dans un **back
buffer en RAM** (dans sa fenêtre user), puis demande au noyau de le présenter :

- `SYS_fb_info(struct fb_info_user*)` (6) : le noyau écrit `{w, h, pitch, bpp}`.
- `SYS_fb_present(buf, len)` (7) : le noyau **valide** `buf` ∈ fenêtre user et `len ≤ w*h*4`,
  puis **copie** le back buffer → framebuffer matériel (le noyau garde le contrôle du FB).
- `SYS_mouse(struct mouse_user*)` (8) : le noyau écrit `{x, y, buttons}`.

Le FB matériel est mappé **superviseur** (U=0) dans les tables de Node-L (`nodel_mm`) :
seul le noyau y accède, pas le ring 3. Fail-closed sur toutes les bornes.

## 2. Souris côté Node-L

IRQ12 routée vers le cœur Node-L (comme IRQ1 clavier), `ps2_mouse_init` appelée par
`node_l_main`, gate 0x22 dans l'IDT Node-L. L'état souris est lu par le compositeur via
`SYS_mouse`. (Gated `NODEL_GUI` pour ne pas perturber le clavier PS/2 nominal.)

## 3. Compositeur (`NODE-L/userland/gui.c`, ring 3)

- Primitives de dessin sur le back buffer (put_pixel/fill_rect/draw_char/string, fonte
  8×16 partagée `HAL/gpu/font8x16.h`).
- **Fenêtres** : `{x, y, w, h, title, bg, z}`. Rendu : desktop (dégradé) → barre de menu
  → fenêtres dans l'ordre de z (barre de titre + corps + texte) → curseur souris.
- **Interaction** : clic gauche sur une barre de titre → la fenêtre passe au premier plan
  (z-order) et suit la souris jusqu'au relâchement (**drag & drop**). Double buffering :
  tout est dessiné dans le back buffer puis `SYS_fb_present` → pas de scintillement.
- Boucle bornée (rdtsc ring 3) pour la testabilité ; logue les événements via `SYS_write`.

## 4. Vérification

`run_phase11_gui.sh` (build `NODEL_GUI=1`) : boot `-vga std` + monitor, `mouse_move` /
`mouse_button` injectés → série : curseur suit la souris, clic détecté, fenêtre déplacée ;
screendump → desktop + fenêtres + barres de titre rendus (couleurs distinctes). Non-régression :
build nominal (shell) → matrice + clavier + fb + web verts.
