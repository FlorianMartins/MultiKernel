/* NEXUS-OS Node-W — surface d'appels système NT (sous-ensemble). */
#pragma once

#define NT_DISPLAY_STRING    1   /* (buf, len) */
#define NT_STORAGE_WRITE     2   /* (lba, buf, len) -> I/O croisée via IPC */
#define NT_STORAGE_READ      3   /* (lba, buf, len) */
#define NT_TERMINATE_PROCESS 4   /* (code) */

#define NT_SYSCALL_VECTOR 0x2E
