#ifndef PANIC_H
#define PANIC_H

#include "isr.h"

/* Prints the message (printf-style) to screen + serial, then halts forever. */
void panic(const char *fmt, ...)
    __attribute__((noreturn, format(printf, 1, 2)));

/* Same as panic(), but also dumps the trap frame registers. */
void panic_regs(registers_t *r, const char *fmt, ...)
    __attribute__((noreturn, format(printf, 2, 3)));

/* Dumps all registers in a trap frame (screen + serial). */
void dump_registers(registers_t *r);

#endif
