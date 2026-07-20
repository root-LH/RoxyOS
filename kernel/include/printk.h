#ifndef PRINTK_H
#define PRINTK_H

#include "types.h"

void printk(const char *str);
void printk_hex(uint32_t value);

#endif