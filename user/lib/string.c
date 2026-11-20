#include <string.h>
#include <stdlib.h>

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;

    while (n--)
        *d++ = *s++;

    return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;

    if (d < s)
    {
        while (n--)
            *d++ = *s++;
    }
    else if (d > s)
    {
        while (n--)
            d[n] = s[n];
    }

    return dest;
}

void *memset(void *dest, int c, size_t n)
{
    unsigned char *d = dest;

    while (n--)
        *d++ = (unsigned char)c;

    return dest;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;

    for (size_t i = 0; i < n; i++)
    {
        if (x[i] != y[i])
            return x[i] - y[i];
    }

    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;

    while (s[n])
        n++;

    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }

    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n > 0; n--, a++, b++)
    {
        if (*a != *b || *a == '\0')
            return (unsigned char)*a - (unsigned char)*b;
    }

    return 0;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;

    while ((*d++ = *src++))
        ;

    return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
    size_t i = 0;

    for (; i < n && src[i]; i++)
        dest[i] = src[i];

    for (; i < n; i++)
        dest[i] = '\0';

    return dest;
}

char *strcat(char *dest, const char *src)
{
    strcpy(dest + strlen(dest), src);

    return dest;
}

char *strchr(const char *s, int c)
{
    for (;; s++)
    {
        if (*s == (char)c)
            return (char *)s;

        if (*s == '\0')
            return NULL;
    }
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;

    for (;; s++)
    {
        if (*s == (char)c)
            last = s;

        if (*s == '\0')
            return (char *)last;
    }
}

char *strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);

    if (copy)
        memcpy(copy, s, len);

    return copy;
}

char *strstr(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);

    for (; *haystack; haystack++)
    {
        if (strncmp(haystack, needle, n) == 0)
            return (char *)haystack;
    }

    return n == 0 ? (char *)haystack : NULL;
}
