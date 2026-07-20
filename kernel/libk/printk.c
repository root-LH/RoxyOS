#include "../include/console.h"
#include "../include/types.h"
#include "../include/vga.h"

void printk(const char *str)
{
    console_write(str);
}

void printk_hex(uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    for (int i = 7; i >= 0; i--)
    {
        char c = hex[(value >> (i * 4)) & 0xF];
        vga_putchar(c);
    }
}