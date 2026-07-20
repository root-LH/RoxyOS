#include "../../include/pit.h"
#include "../../include/io.h"

#define PIT_CHANNEL0   0x40
#define PIT_COMMAND    0x43
#define PIT_FREQUENCY  1193182

volatile uint32_t timer_ticks = 0;

void pit_init(uint32_t frequency)
{
    uint16_t divisor = PIT_FREQUENCY / frequency;

    // Command Byte
    outb(PIT_COMMAND, 0x36);

    // Divisor (Low Byte → High Byte)
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

uint32_t pit_get_ticks(void)
{
    return timer_ticks;
}