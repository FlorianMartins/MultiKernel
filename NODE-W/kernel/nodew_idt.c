/* NEXUS-OS Node-W — IDT du nœud : exceptions 0..31 + gate syscall NT int 0x2e (DPL3). */
#include "kc/types.h"
#include "kc/string.h"
#include "nt.h"

extern u64  isr_table[32];        /* stubs d'exception partagés (COORDINATOR/smp/isr.asm) */
extern void load_idt(void *idt_ptr);
extern void nt_syscall_entry(void);

struct idt_entry {
    u16 off_lo; u16 sel; u8 ist; u8 type_attr; u16 off_mid; u32 off_hi; u32 zero;
} __attribute__((packed));
struct idt_ptr { u16 limit; u64 base; } __attribute__((packed));

static struct idt_entry w_idt[256] __attribute__((aligned(16)));
static struct idt_ptr   w_idt_ptr;

static void set_gate(int v, u64 handler, u8 dpl, u8 type) {
    w_idt[v].off_lo    = handler & 0xFFFF;
    w_idt[v].sel       = 0x08;
    w_idt[v].ist       = 0;
    w_idt[v].type_attr = 0x80 | ((dpl & 3) << 5) | type;
    w_idt[v].off_mid   = (handler >> 16) & 0xFFFF;
    w_idt[v].off_hi    = (handler >> 32) & 0xFFFFFFFF;
    w_idt[v].zero      = 0;
}

void nodew_idt_init(void) {
    memset(w_idt, 0, sizeof(w_idt));
    for (int i = 0; i < 32; i++) set_gate(i, isr_table[i], 0, 0xE);
    set_gate(NT_SYSCALL_VECTOR, (u64)(uintptr_t)&nt_syscall_entry, 3, 0xF);  /* int 0x2e DPL3 */
    w_idt_ptr.limit = sizeof(w_idt) - 1;
    w_idt_ptr.base  = (u64)(uintptr_t)w_idt;
    load_idt(&w_idt_ptr);
}
