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

/* Left and right keys are tracked separately so releasing one keeps the other. */
static int ctrl_left = 0, ctrl_right = 0;
static int alt_left = 0, alt_right = 0;

int keyboard_modifiers(void)
{
    int mods = 0;

    if (shift_pressed)
        mods |= KEYMOD_SHIFT;
    if (ctrl_left || ctrl_right)
        mods |= KEYMOD_CTRL;
    if (alt_left || alt_right)
        mods |= KEYMOD_ALT;
    if (caps_lock)
        mods |= KEYMOD_CAPS;

    return mods;
}

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

/* Pulses the CPU reset line through the 8042 controller. */
void keyboard_reboot(void)
{
    asm volatile("cli");

    /* Wait until the controller's input buffer is empty, then reset. */
    for (int i = 0; i < 100000 && (inb(KEYBOARD_STATUS_PORT) & 0x02); i++)
        ;

    outb(KEYBOARD_STATUS_PORT, 0xFE);

    /* Still here: load an empty IDT and fault (triple fault resets). */
    static const struct { uint16_t limit; uint32_t base; } __attribute__((packed))
        no_idt = { 0, 0 };

    asm volatile("lidt %0; int $3" : : "m"(no_idt));

    for (;;)
        asm volatile("hlt");
}

/* Keys after an 0xE0 prefix (make codes; breaks only matter for modifiers). */
static void extended_key(uint8_t scancode)
{
    int released = scancode & 0x80;

    switch (scancode & 0x7F)
    {
        case 0x1D: ctrl_right = !released; return;
        case 0x38: alt_right = !released;  return;
    }

    if (released)
        return;

    switch (scancode)
    {
        case 0x4B: keyboard_queue_push(KEY_LEFT);   return;
        case 0x4D: keyboard_queue_push(KEY_RIGHT);  return;
        case 0x48: keyboard_queue_push(KEY_UP);     return;
        case 0x50: keyboard_queue_push(KEY_DOWN);   return;
        case 0x47: keyboard_queue_push(KEY_HOME);   return;
        case 0x4F: keyboard_queue_push(KEY_END);    return;
        case 0x53:
            /* Ctrl+Alt+Del */
            if ((ctrl_left || ctrl_right) && (alt_left || alt_right))
                keyboard_reboot();

            keyboard_queue_push(KEY_DELETE);
            return;
        case 0x49: keyboard_queue_push(KEY_PGUP);   return;
        case 0x51: keyboard_queue_push(KEY_PGDN);   return;
        case 0x52: keyboard_queue_push(KEY_INSERT); return;
        case 0x1C: keyboard_queue_push('\n');       return;   /* keypad Enter */
        case 0x35: keyboard_queue_push('/');        return;   /* keypad / */
        default:   return;  /* includes the fake shifts around Print Screen */
    }
}

/* Keypad keys without 0xE0 (NumLock off): the same navigation keys. */
static unsigned char keypad_key(uint8_t scancode)
{
    switch (scancode)
    {
        case 0x47: return KEY_HOME;
        case 0x48: return KEY_UP;
        case 0x49: return KEY_PGUP;
        case 0x4B: return KEY_LEFT;
        case 0x4D: return KEY_RIGHT;
        case 0x4F: return KEY_END;
        case 0x50: return KEY_DOWN;
        case 0x51: return KEY_PGDN;
        case 0x52: return KEY_INSERT;
        case 0x53: return KEY_DELETE;
        case 0x37: return '*';
        case 0x4A: return '-';
        case 0x4E: return '+';
        default:   return 0;
    }
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
        extended_key(scancode);
        return;
    }

    switch (scancode)
    {
        case 0x2A: case 0x36: shift_pressed = 1; return;   /* Shift */
        case 0xAA: case 0xB6: shift_pressed = 0; return;
        case 0x1D: ctrl_left = 1; return;                   /* Left Ctrl */
        case 0x9D: ctrl_left = 0; return;
        case 0x38: alt_left = 1;  return;                   /* Left Alt */
        case 0xB8: alt_left = 0;  return;
        case 0x3A: caps_lock = !caps_lock; return;
    }

    if (scancode & 0x80)
        return;

    unsigned char c = keypad_key(scancode);

    if (c == KEY_DELETE && (ctrl_left || ctrl_right) && (alt_left || alt_right))
        keyboard_reboot();

    if (!c && scancode < sizeof(keymap))
        c = keymap[scancode];

    if (c == 0)
        return;

    if (c >= 'a' && c <= 'z')
    {
        if (shift_pressed ^ caps_lock)
            c = c - 'a' + 'A';
    }
    else if (shift_pressed && scancode < sizeof(shift_keymap) && shift_keymap[scancode])
    {
        c = shift_keymap[scancode];
    }

    if (ctrl_left || ctrl_right)
    {
        /* Ctrl+letter (and Ctrl+[ \ ]) -> control codes 1~29. */
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            c == '[' || c == '\\' || c == ']')
            c = KEY_CTRL(c);
        else if (c > ' ')
            return;     /* nothing to send for Ctrl+digit etc. */

        /* Ctrl+C stops the foreground program instead of being typed. */
        if (c == KEY_CTRL('C') && process_interrupt_foreground())
        {
            printk("^C\n");
            return;
        }
    }

    keyboard_queue_push(c);
}
