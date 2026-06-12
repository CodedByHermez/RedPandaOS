#include "string.h"

#include <stdint.h>

/* These now shuffle megabytes of framebuffer per screen flip, so both have
 * a 32-bit fast path for aligned bulk and fall back to bytes at the edges. */

void *memset(void *dest, int value, size_t count)
{
    uint8_t *d = dest;
    uint8_t v = (uint8_t)value;

    while (count && ((uint32_t)d & 3)) {
        *d++ = v;
        count--;
    }
    uint32_t v32 = v * 0x01010101u;
    uint32_t *d32 = (uint32_t *)d;
    while (count >= 4) {
        *d32++ = v32;
        count -= 4;
    }
    d = (uint8_t *)d32;
    while (count--)
        *d++ = v;
    return dest;
}

void *memcpy(void *dest, const void *src, size_t count)
{
    uint8_t *d = dest;
    const uint8_t *s = src;

    if (((uint32_t)d & 3) == ((uint32_t)s & 3)) {
        while (count && ((uint32_t)d & 3)) {
            *d++ = *s++;
            count--;
        }
        uint32_t *d32 = (uint32_t *)d;
        const uint32_t *s32 = (const uint32_t *)s;
        while (count >= 4) {
            *d32++ = *s32++;
            count -= 4;
        }
        d = (uint8_t *)d32;
        s = (const uint8_t *)s32;
    }
    while (count--)
        *d++ = *s++;
    return dest;
}

void *memmove(void *dest, const void *src, size_t count)
{
    uint8_t *d = dest;
    const uint8_t *s = src;

    if (d < s) {
        while (count--)
            *d++ = *s++;
    } else if (d > s) {
        /* copy backwards so overlapping regions don't clobber themselves */
        d += count;
        s += count;
        while (count--)
            *--d = *--s;
    }
    return dest;
}

int memcmp(const void *a, const void *b, size_t count)
{
    const uint8_t *pa = a;
    const uint8_t *pb = b;
    while (count--) {
        if (*pa != *pb)
            return *pa - *pb;
        pa++;
        pb++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (uint8_t)*a - (uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t count)
{
    while (count && *a && *a == *b) {
        a++;
        b++;
        count--;
    }
    return count ? (uint8_t)*a - (uint8_t)*b : 0;
}
