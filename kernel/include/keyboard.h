#ifndef KEYBOARD_H
#define KEYBOARD_H

#define INPUT_BUFFER_SIZE 256

void keyboard_init(void);
void keyboard_handler(void);
void keyboard_set_input_start(void);

#endif