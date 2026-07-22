#include "include/keyboard.h"
#include "include/io.h"
#include "include/printk.h"
#include "include/vga.h"
#include "include/string.h"
#include "include/shell.h"
#include "include/console.h"

#define KEYBOARD_DATA_PORT   0x60
#define KEYBOARD_STATUS_PORT 0x64

static const char keymap[128] =
{
    0,
    27,     // ESC
    '1','2','3','4','5','6','7','8','9','0',
    '-','=',
    '\b',   // Backspace
    '\t',   // Tab
    'q','w','e','r','t','y','u','i','o','p',
    '[',']',
    '\n',   // Enter

    0,      // Left Ctrl

    'a','s','d','f','g','h','j','k','l',
    ';','\'','`',

    0,      // Left Shift

    '\\',

    'z','x','c','v','b','n','m',
    ',','.','/',

    0,      // Right Shift

    '*',

    0,      // Left Alt

    ' '      // Space
};

static const char shift_keymap[128] =
{
    0,
    27,
    '!','@','#','$','%','^','&','*','(',')',
    '_','+',
    '\b',
    '\t',
    'Q','W','E','R','T','Y','U','I','O','P',
    '{','}',
    '\n',

    0,

    'A','S','D','F','G','H','J','K','L',
    ':','"','~',

    0,

    '|',

    'Z','X','C','V','B','N','M',
    '<','>','?',

    0,

    '*',

    0,

    ' '
};

#define INPUT_BUFFER_SIZE 256

static char input_buffer[INPUT_BUFFER_SIZE];
static uint32_t input_length = 0;
static uint32_t cursor_position = 0;
static int shift_pressed = 0;
static int caps_lock = 0;
static int extended_scancode = 0;
static uint32_t previous_length = 0;

static void keyboard_get_screen_pos(
    uint32_t index,
    uint16_t *x,
    uint16_t *y)
{
    uint32_t pos = console_get_input_start_x() + index;

    *x = pos % VGA_WIDTH;
    *y = console_get_input_start_y() + pos / VGA_WIDTH;
}

static void keyboard_redraw_input(void)
{
    uint16_t x, y;

    for (uint32_t i = 0; i < previous_length; i++)
    {
        keyboard_get_screen_pos(i, &x, &y);
        vga_putentry_at(' ', x, y);
    }

    for (uint32_t i = 0; i < input_length; i++)
    {
        keyboard_get_screen_pos(i, &x, &y);
        vga_putentry_at(input_buffer[i], x, y);
    }

    previous_length = input_length;

    keyboard_get_screen_pos(cursor_position, &x, &y);
    vga_set_cursor(x, y);
}



void keyboard_init(void)
{
    printk("Keyboard Initialized\n");
}

void keyboard_handler(void)
{
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    if (scancode == 0xE0)
    {
        extended_scancode = 1;
        return;
    }
    
    if (extended_scancode)
    {
        extended_scancode = 0;

        switch (scancode)
        {
            case 0x4B:
                if (cursor_position > 0)
                {
                    cursor_position--;
                    keyboard_redraw_input();
                }
                return;

            case 0x4D:
                if (cursor_position < input_length)
                {
                    cursor_position++;
                    keyboard_redraw_input();
                }
                return;

            case 0x48:   // Up
                return;

            case 0x50:   // Down
                return;

            default:
                return;
        }
    }

    /* Left Shift */
    if (scancode == 0x2A)
    {
        shift_pressed = 1;
        return;
    }

    if (scancode == 0xAA)
    {
        shift_pressed = 0;
        return;
    }

    /* Right Shift */
    if (scancode == 0x36)
    {
        shift_pressed = 1;
        return;
    }

    if (scancode == 0x3A)
    {
        caps_lock = !caps_lock;
        return;
    }

    if (scancode == 0xB6)
    {
        shift_pressed = 0;
        return;
    }

    if (scancode & 0x80)
        return;

    if (scancode >= sizeof(keymap))
        return;

    char c = keymap[scancode];

    if (c >= 'a' && c <= 'z')
    {
        if (shift_pressed ^ caps_lock)
            c = c - 'a' + 'A';
    }
    else
    {
        if (shift_pressed)
            c = shift_keymap[scancode];
    }

    if (c == 0)
        return;

    if (c == '\b')
    {
        if (cursor_position == 0)
            return;

        for (uint32_t i = cursor_position - 1; i < input_length - 1; i++)
        {
            input_buffer[i] = input_buffer[i + 1];
        }

        input_length--;
        cursor_position--;
        input_buffer[input_length] = '\0';

        keyboard_redraw_input();
        return;
    }

    if (c == '\n')
    {
        vga_putchar('\n');

        shell_execute(input_buffer);

        input_length = 0;
        cursor_position = 0;
        previous_length = 0;
        input_buffer[0] = '\0';

        shell_print_prompt();

        keyboard_set_input_start();

        return;
    }

    if (input_length >= INPUT_BUFFER_SIZE - 1)
        return;

    for (uint32_t i = input_length; i > cursor_position; i--)
    {
        input_buffer[i] = input_buffer[i - 1];
    }

    input_buffer[cursor_position] = c;

    input_length++;
    cursor_position++;

    input_buffer[input_length] = '\0';
    keyboard_redraw_input();
}

void keyboard_set_input_start(void)
{
    console_set_input_start();
    previous_length = 0;
}