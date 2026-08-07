#include "include/process.h"
#include "include/string.h"
#include "include/scheduler.h"
#include "include/pit.h"

#include "include/printk.h"

static process_t process_table[MAX_PROCESS];

static process_t *current_process = 0;

static context_t *dummy_context = 0;

process_t *process_current(void)
{
    return current_process;
}

void process_set_current(process_t *proc)
{
    current_process = proc;
}

process_t *process_create_kernel(void (*entry)(void))
{
    static uint32_t next_pid = 1;

    for (int i = 0; i < MAX_PROCESS; i++)
    {
        if (process_table[i].state != PROCESS_UNUSED)
            continue;

        process_t *proc = &process_table[i];

        memset(proc, 0, sizeof(process_t));

        proc->pid = next_pid++;
        proc->state = PROCESS_READY;
        proc->type = PROCESS_KERNEL;

        uint32_t *sp =
            (uint32_t *)(proc->stack + PROCESS_STACK_SIZE);

        *--sp = (uint32_t)entry;

        /* context */

        *--sp = 0;      // ebp
        *--sp = 0;      // ebx
        *--sp = 0;      // esi
        *--sp = 0;      // edi

        proc->context = (context_t *)sp;

        return proc;
    }

    return 0;
}

void process_switch(process_t *next)
{
    process_t *prev = current_process;

    current_process = next;

    if (prev && prev->state == PROCESS_RUNNING)
        prev->state = PROCESS_READY;

    next->state = PROCESS_RUNNING;

    process_switch_asm(
        prev ? &prev->context : &dummy_context,
        next->context);
}

void process_yield(void)
{
    process_t *proc = process_current();

    if (!proc)
        return;

    scheduler_clear_resched();

    proc->state = PROCESS_READY;

    scheduler_schedule();
}

void process_exit(void)
{
    process_t *proc = process_current();

    proc->state = PROCESS_EXITED;

    scheduler_remove(proc);

    scheduler_schedule();

    while (1)
    {
        asm volatile("hlt");
    }
}

void process_sleep(uint32_t ticks)
{
    process_t *proc = process_current();

    proc->wakeup_tick = pit_get_ticks() + ticks;

    proc->state = PROCESS_BLOCKED;

    scheduler_schedule();
}