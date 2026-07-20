#include "../include/vga.h"
#include "../include/io.h"

#define VGA_WIDTH   80
#define VGA_HEIGHT  25

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

static void vga_scroll(void)
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