/* NEXUS-OS — chargeur PE/COFF (PE32+ x86_64) pour Node-W. */
#pragma once

#include "kc/types.h"

/* Charge un exécutable PE32+ depuis `image` (taille `size`) : valide MZ/PE, copie les
 * sections à ImageBase+VA (bornées à [win_lo,win_hi)), zéro-remplit, applique les
 * relocations de base. Renvoie le point d'entrée (ImageBase + AddressOfEntryPoint) ou 0.
 * Fail-closed : tout offset/taille hors bornes -> échec. */
u64 pe_load(const u8 *image, u64 size, u64 win_lo, u64 win_hi);
