#include "include/keyboard.h"
#include "include/io.h"
#include "include/printk.h"
#include "include/vga.h"
#include "include/string.h"
#include "include/process.h"
#include "include/scheduler.h"
#include "include/cpu.h"
#include "include/wait.h"

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


static int shift_pressed = 0;
static int caps_lock = 0;
static int extended_scancode = 0;

#define KEYBOARD_QUEUE_SIZE 128
static char keyboard_queue[KEYBOARD_QUEUE_SIZE];

static volatile uint32_t keyboard_head = 0;
static volatile uint32_t keyboard_tail = 0;

static wait_queue_t keyboard_wq = WAIT_QUEUE_INIT;

static void keyboard_queue_push(char c)
{
    uint32_t next =
        (keyboard_tail + 1) % KEYBOARD_QUEUE_SIZE;

    if (next == keyboard_head)
        return;

    keyboard_queue[keyboard_tail] = c;
    keyboard_tail = next;

    wait_queue_wake_all(&keyboard_wq);
}

int keyboard_read(int interruptible)
{
    uint32_t flags = irq_save();

    while (keyboard_head == keyboard_tail)
    {
        if (wait_queue_sleep(&keyboard_wq, interruptible) < 0)
        {
            irq_restore(flags);
            return -1;
        }
    }

    char c = keyboard_queue[keyboard_head];

    keyboard_head = (keyboard_head + 1) % KEYBOARD_QUEUE_SIZE;

    irq_restore(flags);

    return (unsigned char)c;
}

char keyboard_getchar(void)
{
    return (char)keyboard_read(0);
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
            case 0x4B: keyboard_queue_push(KEY_LEFT);  return;
            case 0x4D: keyboard_queue_push(KEY_RIGHT); return;
            case 0x48: keyboard_queue_push(KEY_UP);    return;
            case 0x50: keyboard_queue_push(KEY_DOWN);  return;
            default:   return;
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

    keyboard_queue_push(c);
}
