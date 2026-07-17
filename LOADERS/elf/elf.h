/* NEXUS-OS — chargeur ELF64 (userland Node-L, à terme binaires POSIX). */
#pragma once

#include "kc/types.h"

/* Charge un exécutable ELF64 statique depuis `image` (taille `size`) : valide l'en-tête,
 * copie les segments PT_LOAD à leur p_vaddr (bornés à [win_lo, win_hi)), zéro-remplit le bss.
 * Renvoie le point d'entrée (e_entry) ou 0 en cas d'erreur. Fail-closed. */
u64 elf_load(const u8 *image, u64 size, u64 win_lo, u64 win_hi);
