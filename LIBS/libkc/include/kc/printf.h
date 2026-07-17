/* NEXUS-OS libkc — formatage type printf, indépendant du périphérique de sortie.
 * Le formateur ne connaît aucun matériel : il émet caractère par caractère via
 * un callback fourni (streaming), ce qui le rend testable sur l'hôte. */
#pragma once

#include <stdarg.h>
#include "kc/types.h"

typedef void (*kc_putc_fn)(char c, void *ctx);

/* Formats supportés : %c %s %d/%i %u %x %X %p %%
 * Drapeaux : '0' (zero-pad) ; largeur numérique ; longueurs 'l', 'll', 'z'. */
int kc_vprintf(kc_putc_fn put, void *ctx, const char *fmt, va_list ap);
