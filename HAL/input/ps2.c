/* NEXUS-OS HAL — pilote clavier PS/2 (8042).
 * L'ISR lit le scancode (set 1) au port 0x60, le traduit en ASCII et le pousse dans un
 * ring buffer. Producteur = ISR, consommateur = shell (même cœur -> accès atomiques
 * 32 bits alignés suffisent, pas de verrou). */
#include "ps2.h"
#include "kc/io.h"
#include "serial.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64

#define LAPIC_EOI 0xFEE000B0u

/* --- ring buffer clavier --- */
#define KBD_RING 256
static volatile char kbd_buf[KBD_RING];
static volatile u32  kbd_head;   /* écrit par l'ISR */
static volatile u32  kbd_tail;   /* écrit par le consommateur */

/* --- scancode set 1 -> ASCII (touches usuelles) --- */
static const char sc_normal[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0, 'a','s',
    'd','f','g','h','j','k','l',';','\'','`', 0,'\\','z','x','c','v',
    'b','n','m',',','.','/', 0, '*', 0, ' ', 0,  0,  0,  0,  0,  0,
};
static const char sc_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0, 'A','S',
    'D','F','G','H','J','K','L',':','"','~', 0, '|','Z','X','C','V',
    'B','N','M','<','>','?', 0, '*', 0, ' ', 0,  0,  0,  0,  0,  0,
};

static bool shift_down;

static void kbd_push(char c) {
    u32 h = kbd_head;
    u32 next = (h + 1) & (KBD_RING - 1);
    if (next != (kbd_tail & (KBD_RING - 1))) {   /* pas plein */
        kbd_buf[h & (KBD_RING - 1)] = c;
        kbd_head = h + 1;
    }
}

void kbd_handle(void) {
    u8 sc = inb(PS2_DATA);

    if (sc == 0x2A || sc == 0x36) { shift_down = true;  goto eoi; }   /* Shift down */
    if (sc == 0xAA || sc == 0xB6) { shift_down = false; goto eoi; }   /* Shift up   */
    if (sc & 0x80) goto eoi;                                          /* autre release */

    char c = shift_down ? sc_shift[sc & 0x7F] : sc_normal[sc & 0x7F];
    if (c) kbd_push(c);

eoi:
    *(volatile u32 *)(uintptr_t)LAPIC_EOI = 0;
}

int kbd_getc_nonblock(void) {
    if (kbd_tail == kbd_head) return -1;
    char c = kbd_buf[kbd_tail & (KBD_RING - 1)];
    kbd_tail++;
    return (int)(u8)c;
}

void ps2_kbd_init(void) {
    /* vide le buffer de sortie du 8042 */
    for (int i = 0; i < 16; i++) {
        if (inb(PS2_STATUS) & 1) (void)inb(PS2_DATA);
    }
    kbd_head = kbd_tail = 0;
    shift_down = false;
    serial_printf("[ps2] clavier PS/2 initialisé (IRQ -> vec 0x%x)\n", KBD_IRQ_VECTOR);
}
