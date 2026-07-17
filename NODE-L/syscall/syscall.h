/* NEXUS-OS Node-L — ABI des appels système (partagée noyau <-> userland).
 * Convention : numéro dans rax, args rdi/rsi/rdx, retour rax ; via `int 0x80`. */
#pragma once

#define SYS_write  1   /* (fd, buf, len)  -> octets écrits (fd 1 = console) */
#define SYS_getpid 2   /* ()              -> pid */
#define SYS_yield  3   /* ()              -> 0  (cède la main à l'ordonnanceur) */
#define SYS_exit   4   /* (code)          -> ne revient pas */
#define SYS_read   5   /* (fd, buf, len)  -> octets lus (0 si rien ; NON bloquant) */

#define SYSCALL_VECTOR 0x80
