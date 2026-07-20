#ifndef IRQ_H
#define IRQ_H

#include "types.h"
#include "isr.h"

void irq_install(void);
void irq_handler(registers_t *r);

#endif