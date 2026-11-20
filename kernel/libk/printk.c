#include <stdarg.h>
#include "../include/console.h"
#include "../include/serial.h"
#include "../include/printk.h"
#include "../include/types.h"

#define OUT_CONSOLE 0x1
#define OUT_SERIAL  0x2

static void out_char(int targets, char c)
{
    if (targets & OUT_CONSOLE)
        console_putchar(c);

    if (targets & OUT_SERIAL)
        serial_putchar(c);
}

static void out_str(int targets, const char *s)
{
    while (*s)
        out_char(targets, *s++);
}

void printk(const char *str)
{
    out_str(OUT_CONSOLE | OUT_SERIAL, str);
}

void printk_write(const char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        out_char(OUT_CONSOLE | OUT_SERIAL, buf[i]);
}

void printk_hex(uint32_t value)
{
    kprintf("%08X", value);
}

void printk_dec(uint32_t value)
{
    kprintf("%u", value);
}

/* Writes the digits of value into buf in reverse order and returns the count. */
static int utoa_rev(uint32_t value, unsigned base, int upper, char *buf)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;

    do
    {
        buf[n++] = digits[value % base];
        value /= base;
    } while (value);

    return n;
}

static int vformat(int targets, const char *fmt, va_list ap)
{
    int count = 0;

    for (; *fmt; fmt++)
    {
        if (*fmt != '%')
        {
            out_char(targets, *fmt);
            count++;
            continue;
        }

        fmt++;

        int left = 0;
        char pad = ' ';
        int width = 0;

        for (;; fmt++)
        {
            if (*fmt == '-')
                left = 1;
            else if (*fmt == '0')
                pad = '0';
            else
                break;
        }

        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');

        while (*fmt == 'l' || *fmt == 'h')
            fmt++;

        char tmp[12];
        const char *s = tmp;
        int len = 0;
        int neg = 0;
        int reversed = 0;

        switch (*fmt)
        {
            case 's':
                s = va_arg(ap, const char *);
                if (!s)
                    s = "(null)";
                while (s[len])
                    len++;
                pad = ' ';
                break;

            case 'c':
                tmp[0] = (char)va_arg(ap, int);
                len = 1;
                pad = ' ';
                break;

            case 'd':
            case 'i':
            {
                int32_t v = va_arg(ap, int32_t);
                uint32_t u = (uint32_t)v;
                if (v < 0)
                {
                    neg = 1;
                    u = 0u - u;
                }
                len = utoa_rev(u, 10, 0, tmp);
                reversed = 1;
                break;
            }

            case 'u':
                len = utoa_rev(va_arg(ap, uint32_t), 10, 0, tmp);
                reversed = 1;
                break;

            case 'x':
            case 'X':
                len = utoa_rev(va_arg(ap, uint32_t), 16, *fmt == 'X', tmp);
                reversed = 1;
                break;

            case 'p':
                out_str(targets, "0x");
                count += 2;
                len = utoa_rev((uint32_t)va_arg(ap, void *), 16, 0, tmp);
                reversed = 1;
                if (width < 8)
                    width = 8;
                pad = '0';
                left = 0;
                break;

            case '%':
                tmp[0] = '%';
                len = 1;
                break;

            case '\0':
                return count;

            default:
                /* unknown specifier: print it literally */
                out_char(targets, '%');
                out_char(targets, *fmt);
                count += 2;
                continue;
        }

        int total = len + neg;
        int padding = width > total ? width - total : 0;

        if (neg && pad == '0')
            out_char(targets, '-');

        if (!left)
            for (int i = 0; i < padding; i++)
                out_char(targets, pad);

        if (neg && pad != '0')
            out_char(targets, '-');

        if (reversed)
            for (int i = len - 1; i >= 0; i--)
                out_char(targets, s[i]);
        else
            for (int i = 0; i < len; i++)
                out_char(targets, s[i]);

        if (left)
            for (int i = 0; i < padding; i++)
                out_char(targets, ' ');

        count += total + padding;
    }

    return count;
}

int kvprintf(const char *fmt, va_list ap)
{
    return vformat(OUT_CONSOLE | OUT_SERIAL, fmt, ap);
}

int kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vformat(OUT_CONSOLE | OUT_SERIAL, fmt, ap);
    va_end(ap);
    return n;
}

int klog(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vformat(OUT_SERIAL, fmt, ap);
    va_end(ap);
    return n;
}
