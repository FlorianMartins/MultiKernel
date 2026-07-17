/* NEXUS-OS Node-W — GDT (ring0/ring3) + TSS par cœur.
 * Même disposition que Node-L (chaque nœud est autonome) : 0x08 kcode, 0x10 kdata,
 * 0x18 ucode(DPL3), 0x20 udata(DPL3), 0x28 TSS. gdt_flush/tss_flush partagés (librt). */
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

static u64 w_gdt[7] __attribute__((aligned(16)));
static struct tss64 w_tss __attribute__((aligned(16)));

struct gdt_ptr { u16 limit; u64 base; } __attribute__((packed));
extern void gdt_flush(struct gdt_ptr *p);
extern void tss_flush(u16 sel);

static u64 seg(bool code, int dpl) {
    u64 d = (1ull << 44) | (1ull << 47) | ((u64)(dpl & 3) << 45);
    if (code) d |= (1ull << 43) | (1ull << 53);
    else      d |= (1ull << 41);
    return d;
}

void nodew_gdt_init(u64 kernel_stack_top) {
    memset(&w_tss, 0, sizeof(w_tss));
    w_tss.rsp0 = kernel_stack_top;
    w_tss.io_map_base = sizeof(struct tss64);

    w_gdt[0] = 0;
    w_gdt[1] = seg(true, 0);
    w_gdt[2] = seg(false, 0);
    w_gdt[3] = seg(true, 3);
    w_gdt[4] = seg(false, 3);

    u64 base = (u64)(uintptr_t)&w_tss;
    u64 limit = sizeof(struct tss64) - 1;
    u64 lo = (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16)
           | (0x9ull << 40) | (1ull << 47)
           | (((limit >> 16) & 0xF) << 48) | (((base >> 24) & 0xFF) << 56);
    w_gdt[5] = lo;
    w_gdt[6] = (base >> 32) & 0xFFFFFFFF;

    struct gdt_ptr p = { sizeof(w_gdt) - 1, (u64)(uintptr_t)w_gdt };
    gdt_flush(&p);
    tss_flush(0x28);
}
