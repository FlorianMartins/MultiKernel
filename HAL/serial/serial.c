/* NEXUS-OS HAL — pilote UART 16550 (COM1), 38400 8N1.
 * Chaque domaine possédera à terme son propre port ; pour la Phase 1 le
 * Coordinator utilise COM1 comme console unique. */
#include "serial.h"
#include "kc/io.h"
#include "kc/printf.h"

#include <stdarg.h>

#define COM1 0x3F8

/* Registres relatifs à la base COM1 :
 *  +0 data / divisor-low | +1 IER / divisor-high | +2 FCR | +3 LCR | +4 MCR | +5 LSR */
void serial_init(void) {
    outb(COM1 + 1, 0x00); /* désactive les interruptions */
    outb(COM1 + 3, 0x80); /* DLAB = 1 (accès diviseur) */
    outb(COM1 + 0, 0x03); /* diviseur bas  = 3 -> 38400 bauds */
    outb(COM1 + 1, 0x00); /* diviseur haut = 0 */
    outb(COM1 + 3, 0x03); /* 8 bits, pas de parité, 1 stop ; DLAB = 0 */
    outb(COM1 + 2, 0xC7); /* FIFO activée, vidée, seuil 14 octets */
    outb(COM1 + 4, 0x0B); /* DTR/RTS/OUT2 actifs */
}

static void tx(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0) { /* attend THR vide (LSR bit 5) */ }
    outb(COM1, (u8)c);
}

void serial_putc(char c) {
    if (c == '\n') tx('\r'); /* CRLF pour les terminaux */
    tx(c);
}

void serial_write(const char *s) {
    while (*s) serial_putc(*s++);
}

static void putc_ctx(char c, void *ctx) {
    (void)ctx;
    serial_putc(c);
}

/* Verrou d'affichage : plusieurs cœurs (BSP + AP) écrivent sur COM1 en Phase 2 ;
 * on sérialise ligne par ligne pour ne pas entrelacer les caractères. */
static volatile int s_lock = 0;

static void serial_lock(void) {
    while (__atomic_test_and_set(&s_lock, __ATOMIC_ACQUIRE))
        __asm__ volatile("pause");
}
static void serial_unlock(void) {
    __atomic_clear(&s_lock, __ATOMIC_RELEASE);
}

int serial_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    serial_lock();
    int r = kc_vprintf(putc_ctx, 0, fmt, ap);
    serial_unlock();
    va_end(ap);
    return r;
}
