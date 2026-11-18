#include "ulib.h"

uint32_t strlen(const char *s)
{
    uint32_t n = 0;

    while (s[n])
        n++;

    return n;
}

void print(const char *s)
{
    sys_write(s);
}

void print_int(int value)
{
    char buf[12];
    int i = sizeof(buf) - 1;
    unsigned int u = value < 0 ? 0u - (unsigned int)value : (unsigned int)value;

    buf[i] = '\0';

    do
    {
        buf[--i] = (char)('0' + u % 10);
        u /= 10;
    } while (u);

    if (value < 0)
        buf[--i] = '-';

    sys_write(&buf[i]);
}
