/* =============================================================================
 * RedPandaOS - memory & string routines
 * =============================================================================
 * There is no libc here; these are ours. memset/memcpy must always exist:
 * the compiler emits calls to them on its own for struct copies and array
 * initializers, even in freestanding mode.
 * ============================================================================= */

#ifndef STRING_H
#define STRING_H

#include <stddef.h>

void  *memset(void *dest, int value, size_t count);
void  *memcpy(void *dest, const void *src, size_t count);
void  *memmove(void *dest, const void *src, size_t count);
int    memcmp(const void *a, const void *b, size_t count);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t count);

#endif
