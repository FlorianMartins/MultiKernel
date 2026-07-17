/* NEXUS-OS HAL — console série COM1 (UART 16550), console de debug du domaine COORD. */
#pragma once

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);
int  serial_printf(const char *fmt, ...);
