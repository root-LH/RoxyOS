#ifndef IRQ_H
#define IRQ_H

#include "types.h"
#include "isr.h"

typedef struct irq_switch_result
{
    uint32_t type;
    void *frame;
} irq_switch_result_t;

#define IRQ_RETURN_USER   0
#define IRQ_RETURN_KERNEL 1

void irq_install(void);
registers_t *irq_handler(registers_t *r);

#endif