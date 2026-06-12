/* =============================================================================
 * RedPandaOS - formatted kernel output
 * =============================================================================
 * Our own printf. Supported: %c %s %d %i %u %x %p %%, with optional
 * zero-padding and field width for numbers (e.g. %08x).
 * ============================================================================= */

#ifndef KPRINTF_H
#define KPRINTF_H

#include <stdarg.h>

void kprintf(const char *fmt, ...);
void kvprintf(const char *fmt, va_list ap);

#endif
