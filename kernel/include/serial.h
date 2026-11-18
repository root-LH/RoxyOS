#ifndef SERIAL_H
#define SERIAL_H

#include "types.h"

#define COM1_PORT 0x3F8

int  serial_init(void);
int  serial_ready(void);
void serial_putchar(char c);
void serial_write(const char *str);

#endif
