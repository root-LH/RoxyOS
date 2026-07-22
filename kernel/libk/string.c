#include "../include/string.h"

size_t strlen(const char *str)
{
    size_t len = 0;

    while (str[len] != '\0')
        len++;

    return len;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (*a != *b)
            return *a - *b;

        a++;
        b++;
    }

    return *a - *b;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;

    while (n--)
        *d++ = *s++;

    return dest;
}

void *memset(void *dest, int c, size_t n)
{
    unsigned char *d = dest;

    while (n--)
        *d++ = (unsigned char)c;

    return dest;
}