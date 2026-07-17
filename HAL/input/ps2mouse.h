/* NEXUS-OS HAL — souris PS/2 (port auxiliaire du 8042, IRQ12). */
#pragma once

#include "kc/types.h"

#define MOUSE_IRQ_VECTOR 0x22

struct mouse_state {
    i32 x, y;        /* position curseur, clampée à l'écran */
    u8  buttons;     /* bit0=gauche, bit1=droite, bit2=milieu */
    u32 packets;     /* nb de paquets 3 octets décodés */
};

void ps2_mouse_init(u32 screen_w, u32 screen_h);  /* init contrôleur + souris */
void mouse_handle(void);                          /* appelé par l'ISR (asm) */
const struct mouse_state *mouse_get(void);
