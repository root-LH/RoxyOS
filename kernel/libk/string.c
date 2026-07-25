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

char *strcpy(char *dest, const char *src)
{
    char *ret = dest;

    while (*src)
    {
        *dest = *src;

        dest++;
        src++;
    }

    *dest = '\0';

    return ret;
}

char *strcat(char *dest, const char *src)
{
    char *p = dest;

    while (*p)
        p++;

    while (*src)
        *p++ = *src++;

    *p = '\0';

    return dest;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p1 = a;
    const unsigned char *p2 = b;

    while (n--)
    {
        if (*p1 != *p2)
            return *p1 - *p2;

        p1++;
        p2++;
    }

    return 0;
}