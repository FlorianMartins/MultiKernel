/* NEXUS-OS Node-L — dispatch des appels système (appelé depuis syscall_entry). */
#include "syscall.h"
#include "kc/types.h"
#include "serial.h"

extern void sched_yield(void);
extern volatile u32 g_nodel_user_exited;
extern volatile u32 g_nodel_user_exit_code;
extern u64  g_return_ctx[8];
extern void kctx_restore(u64 *buf, long val);

#define NODEL_PID 100

/* Bornes de la fenêtre user (toute adresse fournie par le ring 3 est validée). */
#define USER_LO 0x4000000ull
#define USER_HI 0x6000000ull

static bool user_range_ok(u64 ptr, u64 len) {
    if (len > (1u << 20)) return false;              /* borne dure */
    if (ptr < USER_LO || ptr >= USER_HI) return false;
    if (ptr + len < ptr || ptr + len > USER_HI) return false;
    return true;
}

u64 syscall_dispatch(u64 num, u64 a1, u64 a2, u64 a3) {
    switch (num) {
    case SYS_write: {
        /* (fd, buf, len) — SÉCURITÉ : borne le pointeur/longueur venus du ring 3. */
        u64 buf = a2, len = a3;
        (void)a1;                                    /* fd ignoré (console) */
        if (!user_range_ok(buf, len)) return (u64)-1;
        serial_write_locked((const char *)(uintptr_t)buf, len);  /* atomique multi-cœurs */
        return len;
    }
    case SYS_getpid:
        return NODEL_PID;

    case SYS_yield:
        sched_yield();
        return 0;

    case SYS_read: {
        /* (fd, buf, len) — non bloquant : lit ce qui est dispo sur la console. */
        u64 buf = a2, len = a3;
        (void)a1;
        if (!user_range_ok(buf, len) || len == 0) return 0;
        char *p = (char *)(uintptr_t)buf;
        u64 n = 0;
        int c;
        while (n < len && (c = serial_getc_nonblock()) >= 0) {
            p[n++] = (char)c;
            if (c == '\n') break;
        }
        return n;
    }

    case SYS_exit:
        g_nodel_user_exit_code = (u32)a1;
        __atomic_store_n(&g_nodel_user_exited, 1, __ATOMIC_RELEASE);
        kctx_restore(g_return_ctx, 1);   /* longjmp : reprend dans node_l_main (ne revient pas) */
        return 0;                        /* jamais atteint */

    default:
        serial_printf("[node-l] syscall inconnu %lu\n", num);
        return (u64)-1;
    }
}
