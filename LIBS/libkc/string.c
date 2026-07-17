/* NEXUS-OS libkc — implémentation des primitives mémoire/chaînes. */
#include "kc/string.h"

/* rep stosb / rep movsb : rapides sur CPU modernes (ERMS) et bien optimisés par
 * l'émulation TCG de QEMU ; évite tout risque de récursion memcpy générée par gcc. */
void *memset(void *dst, int c, size_t n) {
    void *ret = dst;
    __asm__ volatile("rep stosb"
                     : "+D"(dst), "+c"(n)
                     : "a"((u8)c)
                     : "memory");
    return ret;
}

void *memcpy(void *dst, const void *src, size_t n) {
    void *ret = dst;
    __asm__ volatile("rep movsb"
                     : "+D"(dst), "+S"(src), "+c"(n)
                     :
                     : "memory");
    return ret;
}

void *memmove(void *dst, const void *src, size_t n) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const u8 *x = (const u8 *)a, *y = (const u8 *)b;
    while (n--) {
        if (*x != *y) return (int)*x - (int)*y;
        x++; y++;
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (*s++) n++;
    return n;
}
