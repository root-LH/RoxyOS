#include "../include/vga.h"
#include "../include/io.h"
#include "../include/console.h"

static volatile uint16_t* const VGA_MEMORY =
    (volatile uint16_t*)0xB8000;

static uint16_t cursor_x = 0;
static uint16_t cursor_y = 0;

static uint16_t current_color = 0x0F;

static uint16_t vga_entry(char c)
{
    return ((uint16_t)current_color << 8) | c;
}

static inline uint16_t offset(uint16_t x, uint16_t y)
{
    return y * VGA_WIDTH + x;
}

void vga_set_color(
    enum vga_color foreground,
    enum vga_color background)
{
    current_color = foreground | (background << 4);
}

void vga_clear(void)
{
    cursor_x = 0;
    cursor_y = 0;

    for(int i=0;i<VGA_WIDTH*VGA_HEIGHT;i++)
    {
        VGA_MEMORY[i] = vga_entry(' ');
    }
}

void vga_putchar(char c)
{
    if (c == '\n')
    {
        cursor_x = 0;
        cursor_y++;
    }
    else
    {
        VGA_MEMORY[offset(cursor_x,cursor_y)] =
            vga_entry(c);

        cursor_x++;

        if(cursor_x >= VGA_WIDTH)
        {
            cursor_x = 0;
            cursor_y++;
        }
    }

    if(cursor_y >= VGA_HEIGHT)
    {
        vga_scroll();
    }

    vga_update_cursor();
}

void vga_scroll(void)
{
    for(int y=1;y<VGA_HEIGHT;y++)
    {
        for(int x=0;x<VGA_WIDTH;x++)
        {
            VGA_MEMORY[offset(x,y-1)] =
                VGA_MEMORY[offset(x,y)];
        }
    }

    for(int x=0;x<VGA_WIDTH;x++)
    {
        VGA_MEMORY[offset(x,VGA_HEIGHT-1)] =
            vga_entry(' ');
    }

    cursor_y = VGA_HEIGHT-1;
    console_scroll_notify();
}

void vga_puts(const char* str)
{
    while(*str)
    {
        vga_putchar(*str++);
    }
}

void vga_enable_cursor(void)
{
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x00);

    outb(0x3D4, 0x0B);
    outb(0x3D5, 0x0F);
}

void vga_backspace(void)
{
    if (cursor_x == 0 && cursor_y == 0)
        return;

    if (cursor_x == 0)
    {
        cursor_y--;
        cursor_x = VGA_WIDTH - 1;
    }
    else
    {
        cursor_x--;
    }

    VGA_MEMORY[offset(cursor_x, cursor_y)] = vga_entry(' ');

    vga_update_cursor();
}

static void vga_update_cursor(void)
{
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));

    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_move_cursor_left(void)
{
    if (cursor_x == 0)
        return;

    cursor_x--;

    vga_update_cursor();
}

void vga_move_cursor_right(void)
{
    if (cursor_x >= VGA_WIDTH - 1)
        return;

    cursor_x++;

    vga_update_cursor();
}

void vga_set_cursor(uint16_t x, uint16_t y)
{
    y += x / VGA_WIDTH;
    x %= VGA_WIDTH;

    while (y >= VGA_HEIGHT)
    {
        vga_scroll();
        y--;
    }

    cursor_x = x;
    cursor_y = y;

    vga_update_cursor();
}

uint16_t vga_get_cursor_x(void)
{
    return cursor_x;
}

uint16_t vga_get_cursor_y(void)
{
    return cursor_y;
}

void vga_putentry_at(char c, uint16_t x, uint16_t y)
{
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT)
        return;

    VGA_MEMORY[offset(x, y)] = vga_entry(c);
}