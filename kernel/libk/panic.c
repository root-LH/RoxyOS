#include "../include/panic.h"
#include "../include/printk.h"

void panic(const char *msg)
{
    while (1)
    {
        asm volatile("cli");
        asm volatile("hlt");
    }
}