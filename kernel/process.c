#include "include/process.h"
#include "include/string.h"
#include "include/scheduler.h"
#include "include/pit.h"
#include "include/tss.h"

#include "include/printk.h"

extern void process_enter_user_asm(user_context_t *context);

static uint8_t kernel_stacks[MAX_PROCESS][PROCESS_STACK_SIZE];
static uint8_t user_stacks[MAX_PROCESS][USER_STACK_SIZE];

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

        proc->kernel_stack = kernel_stacks[i];

        uint8_t *kstack_top =
            proc->kernel_stack + PROCESS_STACK_SIZE;

        uint32_t *sp = (uint32_t *)kstack_top;

        *--sp = (uint32_t)entry;

        *--sp = 0;      // ebp
        *--sp = 0;      // ebx
        *--sp = 0;      // esi
        *--sp = 0;      // edi

        proc->context = (context_t *)sp;

        kstack_top = (uint8_t *)sp;

        kstack_top -= sizeof(registers_t);

        proc->trapframe = (registers_t *)kstack_top;

        memset(proc->trapframe, 0, sizeof(registers_t));

        proc->trapframe->gs = 0x10;
        proc->trapframe->fs = 0x10;
        proc->trapframe->es = 0x10;
        proc->trapframe->ds = 0x10;

        proc->trapframe->eip = (uint32_t)entry;
        proc->trapframe->cs = 0x08;
        proc->trapframe->eflags = 0x202;

        return proc;
    }

    return 0;
}

process_t *process_create_user(void (*entry)(void))
{
    static uint32_t next_pid = 1000;

    for (int i = 0; i < MAX_PROCESS; i++)
    {
        if (process_table[i].state != PROCESS_UNUSED)
            continue;

        process_t *proc = &process_table[i];

        memset(proc, 0, sizeof(process_t));

        proc->pid = next_pid++;

        proc->state = PROCESS_READY;
        proc->type = PROCESS_USER;

        proc->user_stack = user_stacks[i];

        proc->user_stack_top =
            (uint32_t)(proc->user_stack + USER_STACK_SIZE);

        proc->entry = entry;

        proc->kernel_stack = kernel_stacks[i];

        uint8_t *kstack_top =
            proc->kernel_stack + PROCESS_STACK_SIZE;

        kstack_top -= sizeof(registers_t);

        proc->trapframe = (registers_t *)kstack_top;

        memset(proc->trapframe, 0, sizeof(registers_t));

        proc->trapframe->gs = 0x23;
        proc->trapframe->fs = 0x23;
        proc->trapframe->es = 0x23;
        proc->trapframe->ds = 0x23;

        proc->trapframe->eip = (uint32_t)entry;
        proc->trapframe->cs = 0x1B;
        proc->trapframe->eflags = 0x202;
        proc->trapframe->useresp = proc->user_stack_top;
        proc->trapframe->ss = 0x23;

        kstack_top -= sizeof(user_context_t);

        proc->user_context = (user_context_t *)kstack_top;

        memset(proc->user_context, 0, sizeof(user_context_t));

        proc->user_context->eip = (uint32_t)entry;
        proc->user_context->cs = 0x1B;
        proc->user_context->eflags = 0x202;
        proc->user_context->esp = proc->user_stack_top;
        proc->user_context->ss = 0x23;

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

    if (next->type == PROCESS_USER)
    {
        process_enter_user(next);
        return;
    }

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

    process_t *next = scheduler_schedule();

    if (!next)
        return;

    process_switch(next);
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

    scheduler_request_resched();

    asm volatile("int $32");
}

void process_enter_user(process_t *proc)
{
    if (!proc || !proc->user_context)
        return;

    current_process = proc;
    proc->state = PROCESS_RUNNING;

    tss_set_kernel_stack(
        (uint32_t)(proc->kernel_stack + PROCESS_STACK_SIZE)
    );

    process_enter_user_asm(proc->user_context);
}

registers_t *process_reschedule_from_trap(registers_t *frame)
{
    process_t *current_proc = process_current();

    if (!current_proc)
        return frame;

    process_t *next = scheduler_schedule();

    if (!next || next == current_proc)
        return frame;

    if (current_proc->state == PROCESS_RUNNING)
        current_proc->state = PROCESS_READY;

    next->state = PROCESS_RUNNING;

    process_set_current(next);

    tss_set_kernel_stack(
        (uint32_t)(next->kernel_stack +
                   PROCESS_STACK_SIZE)
    );

    return next->trapframe;
}