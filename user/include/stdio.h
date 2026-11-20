#ifndef _STDIO_H
#define _STDIO_H

#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)
#define BUFSIZ 256

/* A buffered file descriptor. Only stdin, stdout and stderr exist. */
typedef struct FILE
{
    int fd;
    int mode;           /* _IOLBF (line), _IONBF (unbuffered), read */
    int error;
    int eof;
    size_t pos;         /* read: next byte; write: bytes pending */
    size_t len;         /* read: bytes in buf */
    char buf[BUFSIZ];
} FILE;

#define _IOLBF 1
#define _IONBF 2
#define _IOREAD 3

extern FILE *stdin;
extern FILE *stdout;   /* line buffered */
extern FILE *stderr;   /* unbuffered */

/*
 * Format: %d %i %u %x %X %o %p %s %c %%, flags '-' '0' '+' ' ',
 * width and precision (also '*'); 'l', 'h' and 'z' are accepted.
 */
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int fprintf(FILE *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int sprintf(char *buf, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

int putchar(int c);
int puts(const char *s);
int fputc(int c, FILE *f);
int fputs(const char *s, FILE *f);
int fflush(FILE *f);

/* stdin is the console: input arrives a line at a time, echoed. */
int getchar(void);
int fgetc(FILE *f);
char *fgets(char *buf, int size, FILE *f);

#endif
