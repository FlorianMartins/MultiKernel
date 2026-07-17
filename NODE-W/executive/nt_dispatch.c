/* NEXUS-OS Node-W — dispatch des appels NT (appelé depuis nt_syscall_entry).
 * Sépare la logique NT du back-end : NtStorageWrite passe par le canal IPC I/O. */
#include "nt.h"
#include "kc/types.h"
#include "serial.h"
#include "io_channel.h"

extern volatile u32 g_nodew_terminated;
extern volatile u32 g_nodew_exit_code;
extern volatile u32 g_nodew_io_ok;      /* 1 si le round-trip I/O croisé a été vérifié */
extern u64  g_nodew_return_ctx[8];
extern void kctx_restore(u64 *buf, long val);

/* Bornes de la fenêtre Node-W (toute adresse venant du ring 3 est validée). */
#define WLO 0x8000000ull
#define WHI 0xA000000ull

static bool user_range_ok(u64 ptr, u64 len) {
    if (len > (1u << 20)) return false;
    if (ptr < WLO || ptr >= WHI) return false;
    if (ptr + len < ptr || ptr + len > WHI) return false;
    return true;
}

/* checksum identique à celui du back-end (pour vérifier le round-trip). */
static u32 checksum(const u8 *p, u32 n) {
    u32 s = 0;
    for (u32 i = 0; i < n; i++) s = (s * 31u) + p[i];
    return s;
}

u64 nt_dispatch(u64 num, u64 a1, u64 a2, u64 a3) {
    switch (num) {
    case NT_DISPLAY_STRING: {
        u64 buf = a1, len = a2;
        if (!user_range_ok(buf, len)) return (u64)-1;
        serial_write_locked((const char *)(uintptr_t)buf, len);
        return len;
    }

    case NT_STORAGE_WRITE: {
        /* (lba, buf, len) : I/O croisée vers le back-end Node-L via IPC. */
        u32 lba = (u32)a1;
        u64 buf = a2, len = a3;
        if (!user_range_ok(buf, len)) return (u64)-1;
        const u8 *p = (const u8 *)(uintptr_t)buf;
        u32 back_cksum = io_client_write(lba, p, (u32)len);      /* round-trip */
        u32 want = checksum(p, (u32)len);
        if (back_cksum != (u32)-1 && back_cksum == want) {
            g_nodew_io_ok = 1;                                   /* vérifié */
            serial_printf("[node-w] cross-node I/O verified (lba=%u cksum=0x%x)\n", lba, want);
        } else {
            serial_printf("[node-w] cross-node I/O MISMATCH (got=0x%x want=0x%x)\n",
                          back_cksum, want);
        }
        return back_cksum;
    }

    case NT_TERMINATE_PROCESS:
        g_nodew_exit_code = (u32)a1;
        __atomic_store_n(&g_nodew_terminated, 1, __ATOMIC_RELEASE);
        kctx_restore(g_nodew_return_ctx, 1);   /* longjmp -> retour dans node_w_main */
        return 0;

    default:
        serial_printf("[node-w] Nt inconnu %lu\n", num);
        return (u64)-1;
    }
}
