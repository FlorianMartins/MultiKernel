/* NEXUS-OS libkc — primitives mémoire/chaînes freestanding. */
#pragma once

#include "kc/types.h"

void  *memset(void *dst, int c, size_t n);
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
