#ifndef KEYBOARD_H
#define KEYBOARD_H

#define INPUT_BUFFER_SIZE 256

#define KEY_LEFT  ((char)0x81)
#define KEY_RIGHT ((char)0x82)
#define KEY_UP    ((char)0x83)
#define KEY_DOWN  ((char)0x84)

void keyboard_init(void);
void keyboard_handler(void);
/* Blocks until a key is available. */
char keyboard_getchar(void);

/*
 * Same, but returns the key as 0~255 (KEY_* codes are >= 0x80), or -1 when
 * interruptible and the calling process is killed while waiting.
 */
int keyboard_read(int interruptible);

#endif