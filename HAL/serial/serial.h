/* NEXUS-OS HAL — console série COM1 (UART 16550), console de debug du domaine COORD. */
#pragma once

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);
int  serial_printf(const char *fmt, ...);

/* Lecture non bloquante : renvoie l'octet reçu (0..255) ou -1 si rien. */
int  serial_getc_nonblock(void);

/* Écriture d'un buffer, atomique vis-à-vis des autres cœurs (verrou console). */
void serial_write_locked(const char *buf, unsigned long len);
