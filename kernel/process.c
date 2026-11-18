#include "include/process.h"
#include "include/string.h"
#include "include/scheduler.h"
#include "include/pit.h"
#include "include/tss.h"
#include "include/syscall.h"
#include "include/panic.h"
#include "include/paging.h"

#include "include/printk.h"

extern void process_enter_user_asm(user_context_t *context);
extern void process_start_asm(registers_t *frame) __attribute__((noreturn));

static uint8_t kernel_stacks[MAX_PROCESS][PROCESS_STACK_SIZE];

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

process_t *process_get_slot(int i)
{
    if (i < 0 || i >= MAX_PROCESS)
        return 0;

    return &process_table[i];
}

const char *process_state_name(process_state_t state)
{
    switch (state)
    {
        case PROCESS_UNUSED:  return "UNUSED";
        case PROCESS_READY:   return "READY";
        case PROCESS_RUNNING: return "RUNNING";
        case PROCESS_BLOCKED: return "SLEEP";
        case PROCESS_EXITED:  return "ZOMBIE";
        case PROCESS_WAITING: return "WAIT";
    }

    return "?";
}

/*
 * A kernel thread whose entry function returns lands here: the initial
 * trap frame puts this address where `ret` will pop it (see below).
 */
static void kernel_thread_return(void)
{
    process_exit(0);
}

process_t *process_create_kernel(void (*entry)(void))
{
    static uint32_t next_pid = 1;

    process_reap();

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

        /*
         * A ring 0 -> ring 0 iret does not pop useresp/ss, so after the
         * first iret ESP points at the useresp slot. Storing the exit
         * trampoline there makes it the return address of entry().
         */
        proc->trapframe->useresp = (uint32_t)kernel_thread_return;

        return proc;
    }

    return 0;
}

process_t *process_create_user(uint32_t *page_directory,
                               uint32_t entry, uint32_t user_esp)
{
    static uint32_t next_pid = 1000;

    process_reap();

    for (int i = 0; i < MAX_PROCESS; i++)
    {
        if (process_table[i].state != PROCESS_UNUSED)
            continue;

        process_t *proc = &process_table[i];

        memset(proc, 0, sizeof(process_t));

        proc->pid = next_pid++;

        proc->state = PROCESS_READY;
        proc->type = PROCESS_USER;

        proc->page_directory = page_directory;
        proc->user_stack_top = user_esp;

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

        /* First schedule irets straight into ring 3 with this frame. */
        proc->trapframe->eip = entry;
        proc->trapframe->cs = 0x1B;
        proc->trapframe->eflags = 0x202;
        proc->trapframe->useresp = user_esp;
        proc->trapframe->ss = 0x23;

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

void process_kill(process_t *proc, int code)
{
    if (!proc)
        return;

    if (proc->state == PROCESS_UNUSED || proc->state == PROCESS_EXITED)
        return;

    proc->state = PROCESS_EXITED;
    proc->exit_code = code;

    klog("[proc] pid %u exited (code %d)\n", proc->pid, code);

    /* Orphans have nobody to wait for them: reap them automatically. */
    for (int i = 0; i < MAX_PROCESS; i++)
    {
        if (process_table[i].parent == proc)
            process_table[i].parent = 0;
    }

    process_t *parent = proc->parent;

    if (parent && parent->state == PROCESS_WAITING &&
        parent->wait_child == proc)
    {
        parent->state = PROCESS_READY;
        scheduler_request_resched();
    }

    if (proc == current_process)
        scheduler_request_resched();
}

int process_wait(process_t *child)
{
    process_t *self = current_process;

    if (!self || !child)
        return -1;

    while (1)
    {
        asm volatile("cli");

        if (child->state == PROCESS_UNUSED || child->parent != self)
        {
            asm volatile("sti");
            return -1;
        }

        if (child->state == PROCESS_EXITED)
        {
            int code = child->exit_code;

            /* Exit code collected: let process_reap() free the slot. */
            child->parent = 0;
            self->wait_child = 0;

            asm volatile("sti");

            process_reap();

            return code;
        }

        self->wait_child = child;
        self->state = PROCESS_WAITING;
        scheduler_request_resched();

        /* The next interrupt switches away; process_kill() wakes us. */
        asm volatile("sti; hlt");
    }
}

void process_exit(int code)
{
    /*
     * Go through the syscall gate so the switch happens on the normal
     * trap path (isr128 -> process_reschedule_from_trap), without faking
     * a timer interrupt.
     */
    asm volatile("int $0x80" : : "a"(SYS_EXIT), "b"(code) : "memory");

    /* An exited process is never scheduled again. */
    panic("process_exit: pid %u resumed after exit",
          current_process ? current_process->pid : 0);
}

void process_reap(void)
{
    uint32_t flags;

    asm volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");

    for (int i = 0; i < MAX_PROCESS; i++)
    {
        process_t *proc = &process_table[i];

        if (proc->state != PROCESS_EXITED)
            continue;

        /* Its kernel stack is still in use until we switch away from it. */
        if (proc == current_process)
            continue;

        /* The parent has not collected the exit code yet. */
        if (proc->parent)
            continue;

        scheduler_remove(proc);

        /*
         * Safe: we are running on another process's stack and CR3, so the
         * zombie's directory is no longer active.
         */
        if (proc->page_directory)
            vmm_destroy_address_space(proc->page_directory);

        klog("[proc] reaped pid %u (slot %d)\n", proc->pid, i);

        memset(proc, 0, sizeof(process_t));
        proc->state = PROCESS_UNUSED;
    }

    if (flags & 0x200)
        asm volatile("sti");
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

void process_start(process_t *first)
{
    current_process = first;
    first->state = PROCESS_RUNNING;

    tss_set_kernel_stack(
        (uint32_t)(first->kernel_stack + PROCESS_STACK_SIZE)
    );

    process_start_asm(first->trapframe);
}

registers_t *process_reschedule_from_trap(registers_t *frame)
{
    process_t *current_proc = process_current();

    if (!current_proc)
        return frame;

    current_proc->trapframe = frame;

    /* Free zombies from earlier switches; current_proc is skipped. */
    process_reap();

    process_t *next = scheduler_schedule();

    if (!next || next == current_proc)
    {
        if (current_proc->state == PROCESS_EXITED)
            panic("scheduler: no process to run after pid %u exited",
                  current_proc->pid);

        return frame;
    }

    if (current_proc->state == PROCESS_RUNNING)
        current_proc->state = PROCESS_READY;

    next->state = PROCESS_RUNNING;

    process_set_current(next);

    /* Kernel threads run on the kernel directory (page_directory == 0). */
    vmm_switch(next->page_directory);

    tss_set_kernel_stack(
        (uint32_t)(next->kernel_stack +
                   PROCESS_STACK_SIZE)
    );

    return next->trapframe;
}
