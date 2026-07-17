/* NEXUS-OS HAL — framebuffer linéaire (Multiboot2) + rendu 2D minimal.
 * Console = série ; ce module dessine des pixels/rectangles/texte à l'écran graphique.
 * Fondation GUI (le compositeur/userland viendra dessus). */
#pragma once

#include "kc/types.h"
#include "multiboot2.h"

struct fb_info {
    bool present;
    u64  addr;
    u32  pitch;
    u32  width;
    u32  height;
    u8   bpp;
    u8   red_pos, green_pos, blue_pos;   /* décalages des champs couleur */
};

/* Couleur 0xRRGGBB -> valeur pixel selon le format du framebuffer. */
u32  fb_rgb(u8 r, u8 g, u8 b);

void fb_init(const struct mb2_tag_framebuffer *tag);
bool fb_ready(void);
const struct fb_info *fb_get(void);

/* Double buffering : dessine dans un back buffer RAM puis copie à l'écran d'un bloc.
 * Élimine scintillement/déchirure. Nécessite width*height <= FB_BACKBUF_MAX. */
bool fb_enable_backbuffer(void);
void fb_present(void);

void fb_clear(u32 color);
void fb_put_pixel(u32 x, u32 y, u32 color);
void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color);
void fb_draw_char(u32 x, u32 y, char c, u32 fg, u32 bg);
void fb_draw_string(u32 x, u32 y, const char *s, u32 fg, u32 bg);
/* Texte agrandi (facteur d'échelle entier), pour les titres. */
void fb_draw_string_scaled(u32 x, u32 y, const char *s, u32 fg, u32 bg, u32 scale);
