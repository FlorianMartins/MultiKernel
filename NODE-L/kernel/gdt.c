/* NEXUS-OS Node-L — GDT (ring0/ring3) + TSS par cœur.
 * Sélecteurs : 0x08 kcode, 0x10 kdata, 0x18 ucode(DPL3), 0x20 udata(DPL3), 0x28 TSS. */
#include "kc/types.h"
#include "kc/string.h"

struct tss64 {
    u32 reserved0;
    u64 rsp0, rsp1, rsp2;
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3;
    u16 io_map_base;
} __attribute__((packed));

static u64 g_gdt[7] __attribute__((aligned(16)));
static struct tss64 g_tss __attribute__((aligned(16)));

struct gdt_ptr { u16 limit; u64 base; } __attribute__((packed));

extern void gdt_flush(struct gdt_ptr *p);   /* recharge GDT + sélecteurs (asm) */
extern void tss_flush(u16 sel);             /* ltr (asm) */

/* Descripteur de code/data long mode. */
static u64 seg(bool code, int dpl) {
    u64 d = 0;
    d |= (1ull << 44);              /* S = 1 (code/data) */
    d |= (1ull << 47);              /* P = 1 */
    d |= ((u64)(dpl & 3) << 45);    /* DPL */
    if (code) {
        d |= (1ull << 43);          /* type exécutable */
        d |= (1ull << 53);          /* L = 1 (long mode) */
    } else {
        d |= (1ull << 41);          /* type inscriptible */
    }
    return d;
}

void nodel_gdt_init(u64 kernel_stack_top) {
    memset(&g_tss, 0, sizeof(g_tss));
    g_tss.rsp0 = kernel_stack_top;                 /* pile ring0 pour l'entrée syscall */
    g_tss.io_map_base = sizeof(struct tss64);

    g_gdt[0] = 0;
    g_gdt[1] = seg(true, 0);                        /* 0x08 kernel code */
    g_gdt[2] = seg(false, 0);                       /* 0x10 kernel data */
    g_gdt[3] = seg(true, 3);                        /* 0x18 user code */
    g_gdt[4] = seg(false, 3);                       /* 0x20 user data */

    /* Descripteur TSS (système, 16 octets) sur g_gdt[5..6]. */
    u64 base = (u64)(uintptr_t)&g_tss;
    u64 limit = sizeof(struct tss64) - 1;
    u64 lo = 0;
    lo |= (limit & 0xFFFF);
    lo |= (base & 0xFFFFFF) << 16;
    lo |= (0x9ull << 40);                           /* type = TSS disponible 64 bits */
    lo |= (1ull << 47);                             /* présent */
    lo |= (((limit >> 16) & 0xF) << 48);
    lo |= (((base >> 24) & 0xFF) << 56);
    g_gdt[5] = lo;
    g_gdt[6] = (base >> 32) & 0xFFFFFFFF;

    struct gdt_ptr p = { sizeof(g_gdt) - 1, (u64)(uintptr_t)g_gdt };
    gdt_flush(&p);
    tss_flush(0x28);
}
