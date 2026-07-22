#ifndef STRING_H
#define STRING_H

#include <stddef.h>

size_t strlen(const char *str);
int strcmp(const char *a, const char *b);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *dest, int c, size_t n);
char *strcpy(char *dest, const char *src);
char *strcat(char *dest, const char *src);

#endif