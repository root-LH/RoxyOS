#ifndef PRINTK_H
#define PRINTK_H

#include <stdarg.h>
#include "types.h"

void printk(const char *str);
void printk_hex(uint32_t value);
void printk_dec(uint32_t value);

/*
 * Formatted kernel output to the VGA console and COM1.
 * Specifiers: %s %c %d %i %u %x %X %p %%
 * Flags/width: '-', '0', width digits (e.g. %08x, %-10s). 'l'/'h' are accepted and ignored.
 */
int kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int kvprintf(const char *fmt, va_list ap);

/* Serial-only log: goes to COM1, never touches the screen. */
int klog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
