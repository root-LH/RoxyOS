#ifndef CPU_H
#define CPU_H

#include "types.h"

#define EFLAGS_IF 0x200

/* Disables interrupts and returns the previous EFLAGS (for irq_restore). */
static inline uint32_t irq_save(void)
{
    uint32_t flags;

    asm volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");

    return flags;
}

/* Re-enables interrupts only if they were enabled when flags was saved. */
static inline void irq_restore(uint32_t flags)
{
    if (flags & EFLAGS_IF)
        asm volatile("sti" : : : "memory");
}

#endif
