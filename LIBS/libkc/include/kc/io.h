/* NEXUS-OS libkc — accès ports I/O x86 (freestanding). */
#pragma once

#include "kc/types.h"

static inline void outb(u16 port, u8 val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline u8 inb(u16 port) {
    u8 r;
    __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void outw(u16 port, u16 val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline u16 inw(u16 port) {
    u16 r;
    __asm__ volatile("inw %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void outl(u16 port, u32 val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline u32 inl(u16 port) {
    u32 r;
    __asm__ volatile("inl %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
/* Petit délai I/O (écriture sur un port POST inutilisé). */
static inline void io_wait(void) { outb(0x80, 0); }
