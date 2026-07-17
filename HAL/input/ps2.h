/* NEXUS-OS HAL — pilote clavier PS/2 (8042), bufferisé par interruption. */
#pragma once

#include "kc/types.h"

#define KBD_IRQ_VECTOR     0x21
#define TIMER_IRQ_VECTOR   0x20
#define SPURIOUS_VECTOR    0xFF

void ps2_kbd_init(void);           /* vide + active le clavier */
void kbd_handle(void);             /* appelé par l'ISR (asm) : lit 0x60, traduit, bufferise */
int  kbd_getc_nonblock(void);      /* dépile un caractère (0..255) ou -1 si vide */
