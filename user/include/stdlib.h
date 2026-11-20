#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

/* Heap on top of sbrk() (user/lib/malloc.c) */
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);

/* Flushes stdio buffers, then ends the process. */
void exit(int code) __attribute__((noreturn));

int atoi(const char *s);
long strtol(const char *s, char **end, int base);
int abs(int x);

#endif
