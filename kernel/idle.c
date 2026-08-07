#include "include/idle.h"
#include "include/printk.h"
#include "include/scheduler.h"

void idle_task(void)
{
    while (1)
    {
        asm volatile("sti");
        asm volatile("hlt");

        if (scheduler_need_resched())
            process_yield();
    }
}