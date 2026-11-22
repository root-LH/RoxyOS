#ifndef KEYBOARD_H
#define KEYBOARD_H

#define INPUT_BUFFER_SIZE 256

/*
 * KEY_* codes (0x81~) and KEY_CTRL() come from uapi/abi.h: they are ints,
 * so compare them with keys read as unsigned (keyboard_read).
 */
#include "uapi/abi.h"

/* keyboard_modifiers() bits */
#define KEYMOD_SHIFT 0x01
#define KEYMOD_CTRL  0x02
#define KEYMOD_ALT   0x04
#define KEYMOD_CAPS  0x08

void keyboard_init(void);
/*
 * IRQ1. Besides queueing keys: Ctrl+C kills the foreground program (see
 * process_interrupt_foreground) and is only queued when there is none,
 * Ctrl+Alt+Del reboots.
 */
void keyboard_handler(void);

/* Shift/Ctrl/Alt/CapsLock state right now (KEYMOD_*). */
int keyboard_modifiers(void);

/* Resets the machine through the keyboard controller. */
void keyboard_reboot(void) __attribute__((noreturn));
/* Blocks until a key is available. */
char keyboard_getchar(void);

/*
 * Same, but returns the key as 0~255 (KEY_* codes are >= 0x80), or -1 when
 * interruptible and the calling process is killed while waiting.
 */
int keyboard_read(int interruptible);

#endif