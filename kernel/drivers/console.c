#include "../include/console.h"
#include "../include/vga.h"

static uint16_t input_start_x = 0;
static uint16_t input_start_y = 0;

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

void console_set_input_start(void)
{
    input_start_x = vga_get_cursor_x();
    input_start_y = vga_get_cursor_y();
}

uint16_t console_get_input_start_x(void)
{
    return input_start_x;
}

uint16_t console_get_input_start_y(void)
{
    return input_start_y;
}

void console_scroll_notify(void)
{
    if (input_start_y > 0)
        input_start_y--;
}