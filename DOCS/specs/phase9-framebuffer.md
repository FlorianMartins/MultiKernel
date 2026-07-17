# SPEC — Phase 9 : Framebuffer graphique (Multiboot2) + rendu 2D

> Statut : `IMPLEMENTED`. Objectif : accès pixel à l'écran (framebuffer linéaire demandé
> à GRUB) + une lib de rendu 2D minimale (rectangles, texte bitmap) + un splash de démo.
> Fondation de la future GUI (compositeur/userland viendront dessus).

## 1. Obtention du framebuffer

Tag **framebuffer request** (type 5) ajouté à l'en-tête Multiboot2 (`boot.asm`) :
1024×768×32. GRUB configure un mode graphique linéaire et renvoie le tag **framebuffer**
(type 8) dans la MBI : `addr`, `pitch`, `width`, `height`, `bpp`, positions des champs
couleur. Sous QEMU (VGA std) : `0xfd000000`, R@16 G@8 B@0 (pixel = 0x00RRGGBB).
La console reste la **série** → passer en mode graphique ne casse aucun log de boot.

Le framebuffer physique (`0xfd000000` < 4 GiB) est **déjà mappé en identité** par le stub
de boot (pagination 4 GiB) → adressable directement depuis le Coordinator.

## 2. Rendu 2D (`HAL/gpu/fb`)

`fb_init(tag)`, `fb_rgb(r,g,b)`, `fb_put_pixel`, `fb_fill_rect`, `fb_clear`,
`fb_draw_char/string` (fonte bitmap **8×16**), `fb_draw_string_scaled` (titres).
Fonte : `HAL/gpu/font8x16.h`, générée une fois depuis DejaVu Sans Mono
(`TOOLS/image/genfont.py`) et **committée** → aucune dépendance de build.

## 3. Splash (`HAL/gpu/splash`)

Démo/preuve : dégradé de fond, logo « prisme » (triangle dégradé), titre « Prism Axis »,
panneau d'infos (OS/Kernel/Version/CPUs/Model). Dessiné par le Coordinator au boot.
À terme, la GUI vivra en **userland Node-L** (le FB pourra être attribué à un domaine) ;
ici on valide la brique matérielle.

## 4. Vérification

`run_phase9_fb.sh` : boot QEMU (`-vga std`), screendump via monitor + analyse PIL →
présence de pixels accent cyan (logo/titre) + variété de couleurs. + série :
`[fb] 1024x768 32bpp`, `[splash]`. Non-régression : le tag FB ne casse pas le boot
headless (matrice Phases 1–6 verte) ni le banc web/série ni le clavier.

## 5. Restes (documentés)

- **GUI complète** : compositeur, fenêtres, souris (PS/2 souris à faire), en userland.
- **Double buffering** (anti-scintillement), fontes anti-aliasées, accélération GPU :
  couches supérieures, plus tard. On garde léger.
