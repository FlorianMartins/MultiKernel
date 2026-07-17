/* NEXUS-OS HAL — pilote souris PS/2 (8042 port auxiliaire, IRQ12).
 * L'ISR assemble des paquets de 3 octets et met à jour la position/état global. */
#include "ps2mouse.h"
#include "kc/io.h"
#include "serial.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64
#define LAPIC_EOI  0xFEE000B0u

static struct mouse_state g_mouse;
static u32 g_w, g_h;
static u8  pkt[3];
static u8  phase;   /* phase d'assemblage des paquets 3 octets */

/* --- accès contrôleur 8042 avec attente --- */
static void ps2_wait_write(void) { for (int i = 0; i < 100000; i++) if (!(inb(PS2_STATUS) & 2)) return; }
static void ps2_wait_read(void)  { for (int i = 0; i < 100000; i++) if (inb(PS2_STATUS) & 1) return; }

static void mouse_write(u8 val) {
    ps2_wait_write(); outb(PS2_CMD, 0xD4);    /* "prochaine data -> souris" */
    ps2_wait_write(); outb(PS2_DATA, val);
}
static u8 mouse_read(void) { ps2_wait_read(); return inb(PS2_DATA); }

void ps2_mouse_init(u32 screen_w, u32 screen_h) {
    g_w = screen_w; g_h = screen_h;
    g_mouse.x = screen_w / 2;
    g_mouse.y = screen_h / 2;
    g_mouse.buttons = 0;
    g_mouse.packets = 0;

    ps2_wait_write(); outb(PS2_CMD, 0xA8);            /* active le port auxiliaire */

    /* config byte : activer IRQ12 (bit1), horloge aux active (bit5=0) */
    ps2_wait_write(); outb(PS2_CMD, 0x20);
    u8 cfg = mouse_read();
    cfg |= (1 << 1);
    cfg &= ~(1 << 5);
    ps2_wait_write(); outb(PS2_CMD, 0x60);
    ps2_wait_write(); outb(PS2_DATA, cfg);

    mouse_write(0xF6); (void)mouse_read();            /* set defaults (ACK) */
    mouse_write(0xF4); (void)mouse_read();            /* enable data reporting (ACK) */

    /* Vide tout octet résiduel du buffer de sortie + resync du décodeur de paquets :
     * sinon un octet en attente déclenche l'ISR (une fois IF=1) et décale le curseur. */
    for (int i = 0; i < 16; i++) if (inb(PS2_STATUS) & 1) (void)inb(PS2_DATA);
    phase = 0;

    serial_printf("[mouse] souris PS/2 initialisée (IRQ12 -> vec 0x%x), curseur @(%d,%d)\n",
                  MOUSE_IRQ_VECTOR, g_mouse.x, g_mouse.y);
}

/* --- assemblage des paquets 3 octets --- */
void mouse_handle(void) {
    u8 b = inb(PS2_DATA);

    if (phase == 0 && !(b & 0x08)) goto eoi;   /* resync : bit3 doit être à 1 sur b0 */

    pkt[phase++] = b;
    if (phase < 3) goto eoi;
    phase = 0;

    /* décodage : dX/dY 9 bits signés (signe dans b0) */
    i32 dx = (i32)pkt[1] - ((pkt[0] << 4) & 0x100);
    i32 dy = (i32)pkt[2] - ((pkt[0] << 3) & 0x100);

    g_mouse.x += dx;
    g_mouse.y -= dy;                            /* Y écran inversé */
    if (g_mouse.x < 0) g_mouse.x = 0;
    if (g_mouse.y < 0) g_mouse.y = 0;
    if ((u32)g_mouse.x >= g_w) g_mouse.x = g_w - 1;
    if ((u32)g_mouse.y >= g_h) g_mouse.y = g_h - 1;
    g_mouse.buttons = pkt[0] & 0x07;
    g_mouse.packets++;

eoi:
    *(volatile u32 *)(uintptr_t)LAPIC_EOI = 0;
}

const struct mouse_state *mouse_get(void) { return &g_mouse; }
