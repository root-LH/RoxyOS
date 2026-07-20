#include "include/keyboard.h"
#include "include/io.h"
#include "include/printk.h"
#include "include/vga.h"
#include "include/string.h"
#include "include/shell.h"

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

#define INPUT_BUFFER_SIZE 256

static char input_buffer[INPUT_BUFFER_SIZE];
static uint32_t input_length = 0;

void keyboard_init(void)
{
    printk("Keyboard Initialized\n");
}

void keyboard_handler(void)
{
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    if (scancode & 0x80)
        return;

    if (scancode >= sizeof(keymap))
        return;

    char c = keymap[scancode];

    if (c == 0)
        return;

    if (c == '\b')
    {
        if (input_length > 0)
        {
            input_length--;
            input_buffer[input_length] = '\0';
            vga_backspace();
        }
        return;
    }

    if (c == '\n')
    {
        vga_putchar('\n');

        shell_execute(input_buffer);

        input_length = 0;
        input_buffer[0] = '\0';

        shell_print_prompt();

        return;
    }

    if (input_length >= INPUT_BUFFER_SIZE - 1)
        return;

    input_buffer[input_length++] = c;
    input_buffer[input_length] = '\0';

    vga_putchar(c);
}