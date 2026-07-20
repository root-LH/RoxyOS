#ifndef PIT_H
#define PIT_H

#include "types.h"

void pit_init(uint32_t frequency);
uint32_t pit_get_ticks(void);
extern volatile uint32_t timer_ticks;

#endif