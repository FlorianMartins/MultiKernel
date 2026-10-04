/* NEXUS-OS — écran de démarrage graphique (démo framebuffer). */
#pragma once

#include "kc/types.h"

/* Dessine le splash MultiKernel (dégradé, logo, infos système) sur le framebuffer.
 * `ncpus` = nb de cœurs détectés (affiché). No-op si pas de framebuffer. */
void splash_draw(const char *os_name, const char *kernel_name,
                 const char *version, u32 ncpus);
