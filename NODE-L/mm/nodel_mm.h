/* NEXUS-OS Node-L — pagination propre du noeud (dans sa partition). */
#pragma once

#include "kc/types.h"

/* Fenêtre user de Node-L (sous-ensemble de sa partition [64,96MiB)). */
#define NODEL_USER_WIN_BASE  0x4000000ull   /* 64 MiB */
#define NODEL_USER_WIN_SIZE  0x2000000ull   /* 32 MiB */

/* Base de link du userland + sommet de pile user (dans la fenêtre user). */
#define NODEL_USER_LOAD_BASE 0x4400000ull   /* 68 MiB */
#define NODEL_USER_STACK_TOP 0x5000000ull   /* 80 MiB */

/* Construit les tables Node-L (kernel superviseur + fenêtre user) et bascule CR3.
 * Renvoie la physique du PML4. */
u64 nodel_mm_activate(void);
