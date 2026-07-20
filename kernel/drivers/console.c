#include "../include/console.h"
#include "../include/vga.h"

void console_clear(void)
{
    vga_clear();
}

void console_putchar(char c)
{
    vga_putchar(c);
}

void console_write(const char *str)
{
    vga_puts(str);
}