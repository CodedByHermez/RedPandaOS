#include "kprintf.h"

#include <stdint.h>

#include "console.h"

static void print_num(uint32_t value, uint32_t base, int width, char pad)
{
    static const char digits[] = "0123456789abcdef";
    char buf[12];               /* 32-bit max: 10 decimal digits */
    int i = 0;

    if (width > 11)
        width = 11;

    if (value == 0)
        buf[i++] = '0';
    while (value) {
        buf[i++] = digits[value % base];
        value /= base;
    }
    while (i < width)
        buf[i++] = pad;

    while (i--)
        console_putc(buf[i]);
}

void kvprintf(const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            console_putc(*fmt);
            continue;
        }

        fmt++;
        if (!*fmt)
            break;

        char pad = ' ';
        int width = 0;
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        switch (*fmt) {
        case 'c':
            console_putc((char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            console_print(s ? s : "(null)");
            break;
        }
        case 'd':
        case 'i': {
            int v = va_arg(ap, int);
            uint32_t u = (uint32_t)v;
            if (v < 0) {
                console_putc('-');
                u = 0u - u;
            }
            print_num(u, 10, width, pad);
            break;
        }
        case 'u':
            print_num(va_arg(ap, uint32_t), 10, width, pad);
            break;
        case 'x':
            print_num(va_arg(ap, uint32_t), 16, width, pad);
            break;
        case 'p':
            console_print("0x");
            print_num((uint32_t)va_arg(ap, void *), 16, 8, '0');
            break;
        case '%':
            console_putc('%');
            break;
        default:                /* unknown specifier: print it literally */
            console_putc('%');
            console_putc(*fmt);
            break;
        }
    }
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}
