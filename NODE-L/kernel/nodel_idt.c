/* NEXUS-OS Node-L — IDT du noeud : exceptions 0..31 + gate syscall int 0x80 (DPL3). */
#include "kc/types.h"
#include "kc/string.h"
#include "syscall.h"

extern u64  isr_table[32];        /* stubs d'exception (COORDINATOR/smp/isr.asm) */
extern void load_idt(void *idt_ptr);
extern void syscall_entry(void);  /* NODE-L/kernel/entry.asm */

struct idt_entry {
    u16 off_lo; u16 sel; u8 ist; u8 type_attr; u16 off_mid; u32 off_hi; u32 zero;
} __attribute__((packed));
struct idt_ptr { u16 limit; u64 base; } __attribute__((packed));

static struct idt_entry g_idt[256] __attribute__((aligned(16)));
static struct idt_ptr   g_idt_ptr;

static void set_gate(int v, u64 handler, u8 dpl, u8 type) {
    g_idt[v].off_lo    = handler & 0xFFFF;
    g_idt[v].sel       = 0x08;                 /* kernel code Node-L */
    g_idt[v].ist       = 0;
    g_idt[v].type_attr = 0x80 | ((dpl & 3) << 5) | type;  /* P | DPL | type */
    g_idt[v].off_mid   = (handler >> 16) & 0xFFFF;
    g_idt[v].off_hi    = (handler >> 32) & 0xFFFFFFFF;
    g_idt[v].zero      = 0;
}

void nodel_idt_init(void) {
    memset(g_idt, 0, sizeof(g_idt));
    for (int i = 0; i < 32; i++) set_gate(i, isr_table[i], 0, 0xE);   /* interrupt gate DPL0 */
    /* int 0x80 : accessible depuis le ring 3 (DPL3), trap gate (IF conservé). */
    set_gate(SYSCALL_VECTOR, (u64)(uintptr_t)&syscall_entry, 3, 0xF);
    g_idt_ptr.limit = sizeof(g_idt) - 1;
    g_idt_ptr.base  = (u64)(uintptr_t)g_idt;
    load_idt(&g_idt_ptr);
}
