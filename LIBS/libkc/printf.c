/* NEXUS-OS libkc — formateur printf minimal en streaming. */
#include "kc/printf.h"

static void emit(kc_putc_fn put, void *ctx, char c, int *count) {
    put(c, ctx);
    (*count)++;
}

static void emit_uint(kc_putc_fn put, void *ctx, u64 val, unsigned base,
                      bool upper, int width, char pad, int *count) {
    char buf[32];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;

    if (val == 0) buf[i++] = '0';
    while (val) {
        buf[i++] = digits[val % base];
        val /= base;
    }
    for (int p = i; p < width; p++) emit(put, ctx, pad, count);
    while (i > 0) emit(put, ctx, buf[--i], count);
}

static void emit_int(kc_putc_fn put, void *ctx, i64 val, int width, char pad, int *count) {
    if (val < 0) {
        emit(put, ctx, '-', count);
        if (width > 0) width--;
        emit_uint(put, ctx, (u64)(-val), 10, false, width, pad, count);
    } else {
        emit_uint(put, ctx, (u64)val, 10, false, width, pad, count);
    }
}

int kc_vprintf(kc_putc_fn put, void *ctx, const char *fmt, va_list ap) {
    int count = 0;

    for (; *fmt; fmt++) {
        if (*fmt != '%') { emit(put, ctx, *fmt, &count); continue; }
        fmt++;

        char pad = ' ';
        if (*fmt == '0') { pad = '0'; fmt++; }

        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }

        int lng = 0; /* 0=int, 1=long, 2=long long, 3=size_t */
        if (*fmt == 'l') { lng = 1; fmt++; if (*fmt == 'l') { lng = 2; fmt++; } }
        else if (*fmt == 'z') { lng = 3; fmt++; }

        switch (*fmt) {
        case 'c': {
            char c = (char)va_arg(ap, int);
            emit(put, ctx, c, &count);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = 0; for (const char *t = s; *t; t++) len++;
            for (int p = len; p < width; p++) emit(put, ctx, ' ', &count);
            for (; *s; s++) emit(put, ctx, *s, &count);
            break;
        }
        case 'd': case 'i': {
            i64 v = (lng >= 2) ? va_arg(ap, long long)
                  : (lng == 1) ? va_arg(ap, long)
                               : va_arg(ap, int);
            emit_int(put, ctx, v, width, pad, &count);
            break;
        }
        case 'u': {
            u64 v = (lng >= 2) ? va_arg(ap, unsigned long long)
                  : (lng == 1) ? va_arg(ap, unsigned long)
                  : (lng == 3) ? (u64)va_arg(ap, size_t)
                               : va_arg(ap, unsigned int);
            emit_uint(put, ctx, v, 10, false, width, pad, &count);
            break;
        }
        case 'x': case 'X': {
            bool up = (*fmt == 'X');
            u64 v = (lng >= 2) ? va_arg(ap, unsigned long long)
                  : (lng == 1) ? va_arg(ap, unsigned long)
                  : (lng == 3) ? (u64)va_arg(ap, size_t)
                               : va_arg(ap, unsigned int);
            emit_uint(put, ctx, v, 16, up, width, pad, &count);
            break;
        }
        case 'p': {
            u64 v = (u64)(uintptr_t)va_arg(ap, void *);
            emit(put, ctx, '0', &count);
            emit(put, ctx, 'x', &count);
            emit_uint(put, ctx, v, 16, false, 16, '0', &count);
            break;
        }
        case '%':
            emit(put, ctx, '%', &count);
            break;
        default:
            emit(put, ctx, '%', &count);
            if (*fmt) emit(put, ctx, *fmt, &count);
            break;
        }
    }
    return count;
}
