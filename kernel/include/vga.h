#ifndef VGA_H
#define VGA_H

#include <stdint.h>

enum vga_color
{
    VGA_COLOR_BLACK         = 0x0,
    VGA_COLOR_BLUE          = 0x1,
    VGA_COLOR_GREEN         = 0x2,
    VGA_COLOR_CYAN          = 0x3,
    VGA_COLOR_RED           = 0x4,
    VGA_COLOR_MAGENTA       = 0x5,
    VGA_COLOR_BROWN         = 0x6,
    VGA_COLOR_LIGHT_GREY    = 0x7,
    VGA_COLOR_DARK_GREY     = 0x8,
    VGA_COLOR_LIGHT_BLUE    = 0x9,
    VGA_COLOR_LIGHT_GREEN   = 0xA,
    VGA_COLOR_LIGHT_CYAN    = 0xB,
    VGA_COLOR_LIGHT_RED     = 0xC,
    VGA_COLOR_LIGHT_MAGENTA = 0xD,
    VGA_COLOR_YELLOW        = 0xE,
    VGA_COLOR_WHITE         = 0xF
};

void vga_clear(void);
void vga_putchar(char c);
void vga_puts(const char* str);
void vga_set_color(
    enum vga_color foreground,
    enum vga_color background
);
void vga_enable_cursor(void);
void vga_backspace(void);

static void vga_scroll(void);
static void vga_update_cursor(void);

#endif