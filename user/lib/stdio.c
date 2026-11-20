#include <stdio.h>
#include <string.h>
#include <unistd.h>

static FILE stdin_file  = { STDIN_FILENO,  _IOREAD, 0, 0, 0, 0, { 0 } };
static FILE stdout_file = { STDOUT_FILENO, _IOLBF,  0, 0, 0, 0, { 0 } };
static FILE stderr_file = { STDERR_FILENO, _IONBF,  0, 0, 0, 0, { 0 } };

FILE *stdin  = &stdin_file;
FILE *stdout = &stdout_file;
FILE *stderr = &stderr_file;

/* ------------------------------------------------------------------ */
/* Output                                                              */
/* ------------------------------------------------------------------ */

static int write_all(int fd, const char *buf, size_t len)
{
    while (len > 0)
    {
        ssize_t n = write(fd, buf, len);

        if (n <= 0)
            return -1;

        buf += n;
        len -= (size_t)n;
    }

    return 0;
}

int fflush(FILE *f)
{
    if (!f || f->mode == _IOREAD)
        return 0;

    if (f->pos == 0)
        return 0;

    int r = write_all(f->fd, f->buf, f->pos);

    f->pos = 0;

    if (r != 0)
    {
        f->error = 1;
        return EOF;
    }

    return 0;
}

int fputc(int c, FILE *f)
{
    if (f->mode == _IOREAD)
        return EOF;

    if (f->mode == _IONBF)
    {
        char ch = (char)c;

        return write_all(f->fd, &ch, 1) == 0 ? (unsigned char)c : EOF;
    }

    f->buf[f->pos++] = (char)c;

    if (f->pos == BUFSIZ || c == '\n')
    {
        if (fflush(f) != 0)
            return EOF;
    }

    return (unsigned char)c;
}

int putchar(int c)
{
    return fputc(c, stdout);
}

int fputs(const char *s, FILE *f)
{
    for (; *s; s++)
    {
        if (fputc(*s, f) == EOF)
            return EOF;
    }

    return 0;
}

int puts(const char *s)
{
    if (fputs(s, stdout) == EOF)
        return EOF;

    return putchar('\n') == EOF ? EOF : 0;
}

/* ------------------------------------------------------------------ */
/* Formatting                                                          */
/* ------------------------------------------------------------------ */

typedef struct
{
    void (*emit)(void *ctx, char c);
    void *ctx;
    int count;
} out_t;

static void out(out_t *o, char c)
{
    o->emit(o->ctx, c);
    o->count++;
}

static void pad(out_t *o, char c, int n)
{
    while (n-- > 0)
        out(o, c);
}

#define F_LEFT  0x01
#define F_ZERO  0x02
#define F_PLUS  0x04
#define F_SPACE 0x08

static void format_number(out_t *o, unsigned long value, int negative,
                          unsigned base, int upper, int flags, int width,
                          int precision, const char *prefix)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[24];
    int len = 0;

    if (!(value == 0 && precision == 0))
    {
        do
        {
            buf[len++] = digits[value % base];
            value /= base;
        } while (value);
    }

    char sign = negative ? '-' : (flags & F_PLUS) ? '+' : (flags & F_SPACE) ? ' ' : 0;
    int zeros = precision > len ? precision - len : 0;
    int body = len + zeros + (sign ? 1 : 0) + (int)strlen(prefix);

    /* '0' pads between the sign and the digits; ignored with a precision. */
    if ((flags & F_ZERO) && !(flags & F_LEFT) && precision < 0 && width > body)
    {
        zeros += width - body;
        body = width;
    }

    if (!(flags & F_LEFT))
        pad(o, ' ', width - body);

    if (sign)
        out(o, sign);

    for (; *prefix; prefix++)
        out(o, *prefix);

    pad(o, '0', zeros);

    while (len > 0)
        out(o, buf[--len]);

    if (flags & F_LEFT)
        pad(o, ' ', width - body);
}

static int format(out_t *o, const char *fmt, va_list ap)
{
    for (; *fmt; fmt++)
    {
        if (*fmt != '%')
        {
            out(o, *fmt);
            continue;
        }

        fmt++;

        int flags = 0;

        for (;; fmt++)
        {
            if (*fmt == '-')      flags |= F_LEFT;
            else if (*fmt == '0') flags |= F_ZERO;
            else if (*fmt == '+') flags |= F_PLUS;
            else if (*fmt == ' ') flags |= F_SPACE;
            else break;
        }

        int width = 0;

        if (*fmt == '*')
        {
            width = va_arg(ap, int);

            if (width < 0)
            {
                flags |= F_LEFT;
                width = -width;
            }
            fmt++;
        }
        else
        {
            while (*fmt >= '0' && *fmt <= '9')
                width = width * 10 + (*fmt++ - '0');
        }

        int precision = -1;

        if (*fmt == '.')
        {
            fmt++;
            precision = 0;

            if (*fmt == '*')
            {
                precision = va_arg(ap, int);
                fmt++;
            }
            else
            {
                while (*fmt >= '0' && *fmt <= '9')
                    precision = precision * 10 + (*fmt++ - '0');
            }
        }

        /* int, long and size_t are all 32 bits here. */
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z')
            fmt++;

        switch (*fmt)
        {
            case 'd':
            case 'i':
            {
                int v = va_arg(ap, int);
                unsigned long u = v < 0 ? 0ul - (unsigned long)v : (unsigned long)v;

                format_number(o, u, v < 0, 10, 0, flags, width, precision, "");
                break;
            }

            case 'u':
                format_number(o, va_arg(ap, unsigned int), 0, 10, 0,
                              flags & ~(F_PLUS | F_SPACE), width, precision, "");
                break;

            case 'x':
            case 'X':
                format_number(o, va_arg(ap, unsigned int), 0, 16, *fmt == 'X',
                              flags & ~(F_PLUS | F_SPACE), width, precision, "");
                break;

            case 'o':
                format_number(o, va_arg(ap, unsigned int), 0, 8, 0,
                              flags & ~(F_PLUS | F_SPACE), width, precision, "");
                break;

            case 'p':
                format_number(o, (unsigned long)va_arg(ap, void *), 0, 16, 0,
                              flags & ~(F_PLUS | F_SPACE), width, 8, "0x");
                break;

            case 'c':
                if (!(flags & F_LEFT))
                    pad(o, ' ', width - 1);

                out(o, (char)va_arg(ap, int));

                if (flags & F_LEFT)
                    pad(o, ' ', width - 1);
                break;

            case 's':
            {
                const char *s = va_arg(ap, const char *);

                if (!s)
                    s = "(null)";

                int len = 0;

                while (s[len] && (precision < 0 || len < precision))
                    len++;

                if (!(flags & F_LEFT))
                    pad(o, ' ', width - len);

                for (int i = 0; i < len; i++)
                    out(o, s[i]);

                if (flags & F_LEFT)
                    pad(o, ' ', width - len);
                break;
            }

            case '%':
                out(o, '%');
                break;

            case '\0':
                return o->count;

            default:
                out(o, '%');
                out(o, *fmt);
                break;
        }
    }

    return o->count;
}

static void emit_file(void *ctx, char c)
{
    fputc(c, ctx);
}

typedef struct
{
    char *buf;
    size_t size;
    size_t len;
} strbuf_t;

static void emit_string(void *ctx, char c)
{
    strbuf_t *sb = ctx;

    if (sb->len + 1 < sb->size)
        sb->buf[sb->len] = c;

    sb->len++;
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    out_t o = { emit_file, f, 0 };

    return format(&o, fmt, ap);
}

int vprintf(const char *fmt, va_list ap)
{
    return vfprintf(stdout, fmt, ap);
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    strbuf_t sb = { buf, size, 0 };
    out_t o = { emit_string, &sb, 0 };

    int n = format(&o, fmt, ap);

    if (size > 0)
        buf[sb.len < size ? sb.len : size - 1] = '\0';

    return n;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(stdout, fmt, ap);
    va_end(ap);

    return n;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap);

    return n;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);

    return n;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, (size_t)-1 / 2, fmt, ap);
    va_end(ap);

    return n;
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

int fgetc(FILE *f)
{
    if (f->mode != _IOREAD)
        return EOF;

    if (f->pos == f->len)
    {
        /* Show any pending prompt before blocking for input. */
        fflush(stdout);

        ssize_t n = read(f->fd, f->buf, BUFSIZ);

        if (n <= 0)
        {
            if (n == 0)
                f->eof = 1;
            else
                f->error = 1;

            return EOF;
        }

        f->pos = 0;
        f->len = (size_t)n;
    }

    return (unsigned char)f->buf[f->pos++];
}

int getchar(void)
{
    return fgetc(stdin);
}

char *fgets(char *buf, int size, FILE *f)
{
    int i = 0;

    if (size <= 0)
        return NULL;

    while (i < size - 1)
    {
        int c = fgetc(f);

        if (c == EOF)
            break;

        buf[i++] = (char)c;

        if (c == '\n')
            break;
    }

    if (i == 0)
        return NULL;

    buf[i] = '\0';

    return buf;
}
