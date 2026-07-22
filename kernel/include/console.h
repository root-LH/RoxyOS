#ifndef CONSOLE_H
#define CONSOLE_H

#include "types.h"

void console_clear(void);
void console_putchar(char c);
void console_write(const char *str);

void console_set_input_start(void);

uint16_t console_get_input_start_x(void);
uint16_t console_get_input_start_y(void);

void console_scroll_notify(void);

#endif