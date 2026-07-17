/* NEXUS-OS libkc — primitives CPU x86_64 (freestanding). */
#pragma once

#include "kc/types.h"

static inline void cpuid_raw(u32 leaf, u32 *a, u32 *b, u32 *c, u32 *d) {
    __asm__ volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(0));
}

/* APIC ID initial (CPUID.1:EBX[31:24]) — sans MMIO LAPIC. */
static inline u32 cpu_apic_id(void) {
    u32 a, b, c, d;
    cpuid_raw(1, &a, &b, &c, &d);
    return b >> 24;
}

static inline u64 read_cr2(void) {
    u64 v;
    __asm__ volatile("mov %%cr2, %0" : "=r"(v));
    return v;
}

static inline void cpu_relax(void) { __asm__ volatile("pause"); }

static inline void hlt_forever(void) {
    for (;;) __asm__ volatile("cli; hlt");
}
