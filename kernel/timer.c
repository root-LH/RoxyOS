#include "include/timer.h"
#include "include/pit.h"

void sleep(uint32_t ms)
{
    uint32_t start = pit_get_ticks();

    uint32_t target = ms / 10;

    while ((pit_get_ticks() - start) < target)
    {
        asm volatile("hlt");
    }
}