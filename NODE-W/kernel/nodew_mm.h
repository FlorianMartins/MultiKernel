/* NEXUS-OS Node-W — pagination propre du nœud (dans sa partition). */
#pragma once

#include "kc/types.h"

/* Fenêtre user de Node-W (sa partition [128,160MiB)). */
#define NODEW_USER_WIN_BASE  0x8000000ull   /* 128 MiB */
#define NODEW_USER_WIN_SIZE  0x2000000ull   /* 32 MiB */

#define NODEW_IMAGE_BASE     0x8400000ull   /* 132 MiB : ImageBase du PE */
#define NODEW_USER_STACK_TOP 0x9000000ull   /* 144 MiB : sommet de pile user */

u64 nodew_mm_activate(void);
