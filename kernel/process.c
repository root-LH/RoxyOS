#include "include/process.h"
#include "include/string.h"
#include "include/scheduler.h"
#include "include/pit.h"
#include "include/tss.h"
#include "include/syscall.h"
#include "include/panic.h"
#include "include/paging.h"
#include "include/cpu.h"
#include "include/file.h"
#include "include/memory.h"

#include "include/printk.h"

extern void process_start_asm(registers_t *frame) __attribute__((noreturn));

static uint8_t kernel_stacks[MAX_PROCESS][PROCESS_STACK_SIZE];

static process_t process_table[MAX_PROCESS];
static process_t *current_process = 0;

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

void process_set_name(process_t *proc, const char *name)
{
    /* Keep only the last path component, truncated to fit. */
    for (const char *p = name; *p; p++)
    {
        if (*p == '/')
            name = p + 1;
    }

    uint32_t i = 0;

    for (; name[i] && i < PROCESS_NAME_LEN - 1; i++)
        proc->name[i] = name[i];

    proc->name[i] = '\0';
}

process_t *process_find(uint32_t pid)
{
    for (int i = 0; i < MAX_PROCESS; i++)
    {
        process_t *proc = &process_table[i];

        if (proc->state != PROCESS_UNUSED && proc->pid == pid)
            return proc;
    }

    return 0;
}

/*
 * Claims a free slot and gives it a pid and a kernel stack with room for
 * the initial trap frame. The process is not runnable until the caller
 * fills in the frame and calls scheduler_add().
 */
static process_t *process_alloc(process_type_t type)
{
    /* Kernel threads count from 1, user processes from 1000. */
    static uint32_t next_kernel_pid = 1;
    static uint32_t next_user_pid = 1000;

    process_reap();

    uint32_t flags = irq_save();

    for (int i = 0; i < MAX_PROCESS; i++)
    {
        process_t *proc = &process_table[i];

        if (proc->state != PROCESS_UNUSED)
            continue;

        memset(proc, 0, sizeof(process_t));

        proc->pid = type == PROCESS_USER ? next_user_pid++ : next_kernel_pid++;
        proc->type = type;
        proc->state = PROCESS_READY;

        strcpy(proc->cwd, "/");

        proc->kernel_stack = kernel_stacks[i];
        proc->trapframe = (registers_t *)
            (proc->kernel_stack + PROCESS_STACK_SIZE - sizeof(registers_t));

        memset(proc->trapframe, 0, sizeof(registers_t));

        irq_restore(flags);
        return proc;
    }

    irq_restore(flags);
    return 0;
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
    process_t *proc = process_alloc(PROCESS_KERNEL);

    if (!proc)
        return 0;

    registers_t *tf = proc->trapframe;

    tf->gs = tf->fs = tf->es = tf->ds = 0x10;

    tf->eip = (uint32_t)entry;
    tf->cs = 0x08;
    tf->eflags = 0x202;

    /*
     * A ring 0 -> ring 0 iret does not pop useresp/ss, so after the
     * first iret ESP points at the useresp slot. Storing the exit
     * trampoline there makes it the return address of entry().
     */
    tf->useresp = (uint32_t)kernel_thread_return;

    return proc;
}

process_t *process_create_user(uint32_t *page_directory,
                               uint32_t entry, uint32_t user_esp)
{
    process_t *proc = process_alloc(PROCESS_USER);

    if (!proc)
        return 0;

    proc->page_directory = page_directory;

    registers_t *tf = proc->trapframe;

    tf->gs = tf->fs = tf->es = tf->ds = 0x23;

    /* First schedule irets straight into ring 3 with this frame. */
    tf->eip = entry;
    tf->cs = 0x1B;
    tf->eflags = 0x202;
    tf->useresp = user_esp;
    tf->ss = 0x23;

    return proc;
}

int process_fork(registers_t *frame)
{
    process_t *parent = current_process;

    if (!parent || parent->type != PROCESS_USER)
        return -1;

    uint32_t *pd = vmm_clone_address_space(parent->page_directory);

    if (!pd)
        return -1;

    process_t *child = process_alloc(PROCESS_USER);

    if (!child)
    {
        vmm_destroy_address_space(pd);
        return -1;
    }

    child->page_directory = pd;
    child->heap_start = parent->heap_start;
    child->brk = parent->brk;
    child->parent = parent;
    memcpy(child->name, parent->name, PROCESS_NAME_LEN);
    strcpy(child->cwd, parent->cwd);

    fd_copy(child, parent);

    /* The child resumes from the same int 0x80, with fork() returning 0. */
    *child->trapframe = *frame;
    child->trapframe->eax = 0;

    klog("[proc] pid %u forked pid %u\n", parent->pid, child->pid);

    scheduler_add(child);

    return (int)child->pid;
}

int process_waitpid(int pid, int *status, int options)
{
    process_t *self = current_process;

    if (!self)
        return -1;

    uint32_t flags = irq_save();

    while (1)
    {
        int found = 0;

        for (int i = 0; i < MAX_PROCESS; i++)
        {
            process_t *child = &process_table[i];

            if (child->state == PROCESS_UNUSED || child->parent != self)
                continue;

            if (pid != -1 && child->pid != (uint32_t)pid)
                continue;

            found = 1;

            if (child->state != PROCESS_EXITED)
                continue;

            int child_pid = (int)child->pid;

            if (status)
                *status = child->exit_code;

            /* Exit code collected: let process_reap() free the slot. */
            child->parent = 0;

            irq_restore(flags);

            process_reap();

            return child_pid;
        }

        if (!found || (options & WNOHANG))
        {
            irq_restore(flags);
            return found ? 0 : -1;
        }

        /* process_kill() wakes us when one of our children exits. */
        if (wait_queue_sleep(&self->child_wq, 1) < 0)
        {
            irq_restore(flags);
            return -1;
        }
    }
}

int process_signal_kill(uint32_t pid)
{
    uint32_t flags = irq_save();
    process_t *proc = process_find(pid);

    if (!proc || proc->state == PROCESS_EXITED || proc->type != PROCESS_USER)
    {
        irq_restore(flags);
        return -1;
    }

    proc->killed = 1;

    /*
     * The latest trap frame says where the process is. For the caller of
     * kill() it is the int 0x80 frame (user mode); from an IRQ it is the
     * interrupted context, which may be in the middle of a syscall.
     */
    if ((proc->trapframe->cs & 3) == 3)
    {
        /*
         * Running or preempted in user mode: it holds no kernel state, so
         * it can go right away. (Ourselves: the trap path switches away.)
         */
        process_kill(proc, EXIT_KILLED);
    }
    else if (proc->state == PROCESS_WAITING && proc->wait_interruptible)
    {
        /* Asleep in a syscall: wake it so the syscall fails and it exits. */
        wait_queue_cancel(proc);
        proc->state = PROCESS_READY;
        scheduler_request_resched();
    }
    /* Otherwise it exits when its current syscall returns (syscall_handler). */

    irq_restore(flags);

    return 0;
}

/* The program the shell is waiting for; Ctrl+C goes to it. */
static uint32_t foreground_pid = 0;

void process_set_foreground(uint32_t pid)
{
    foreground_pid = pid;
}

/* Is proc the foreground process or one of its descendants? */
static int in_foreground(process_t *proc)
{
    for (int depth = 0; proc && depth < MAX_PROCESS; depth++)
    {
        if (proc->pid == foreground_pid)
            return 1;

        proc = proc->parent;
    }

    return 0;
}

int process_interrupt_foreground(void)
{
    uint32_t flags = irq_save();
    uint32_t victims[MAX_PROCESS];
    int count = 0;

    if (foreground_pid)
    {
        /* Collect first: killing a parent orphans its children (parent = 0). */
        for (int i = 0; i < MAX_PROCESS; i++)
        {
            process_t *proc = &process_table[i];

            if (proc->state != PROCESS_UNUSED && proc->state != PROCESS_EXITED &&
                proc->type == PROCESS_USER && in_foreground(proc))
                victims[count++] = proc->pid;
        }
    }

    for (int i = 0; i < count; i++)
        process_signal_kill(victims[i]);

    irq_restore(flags);

    return count;
}

int32_t process_sbrk(process_t *proc, int32_t increment)
{
    uint32_t old_brk = proc->brk;
    uint32_t new_brk = old_brk + (uint32_t)increment;

    if (increment == 0)
        return (int32_t)old_brk;

    uint32_t first = PAGE_ALIGN_UP(old_brk);

    if (increment > 0)
    {
        if (new_brk < old_brk || new_brk > USER_HEAP_LIMIT)
            return -1;

        uint32_t last = PAGE_ALIGN_UP(new_brk);

        /*
         * Refuse up front what cannot fit, instead of taking every free
         * frame (with interrupts off) only to give them all back.
         */
        if ((last - first) / PAGE_SIZE > pmm_free_frame_count())
            return -1;

        for (uint32_t va = first; va < last; va += PAGE_SIZE)
        {
            uint32_t frame = (uint32_t)pmm_alloc_frame();

            if (frame)
            {
                memset((void *)P2V(frame), 0, PAGE_SIZE);

                if (vmm_map(proc->page_directory, va, frame,
                            PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) == 0)
                    continue;

                pmm_free_frame((void *)frame);
            }

            /* Out of memory: undo this call's pages. */
            for (uint32_t undo = first; undo < va; undo += PAGE_SIZE)
                pmm_free_frame((void *)vmm_unmap(proc->page_directory, undo));

            return -1;
        }
    }
    else
    {
        if (new_brk < proc->heap_start || new_brk > old_brk)
            return -1;

        for (uint32_t va = PAGE_ALIGN_UP(new_brk); va < first; va += PAGE_SIZE)
        {
            uint32_t frame = vmm_unmap(proc->page_directory, va);

            if (frame)
                pmm_free_frame((void *)frame);
        }
    }

    proc->brk = new_brk;

    return (int32_t)old_brk;
}

void process_block(void)
{
    scheduler_request_resched();

    /* isr128 sees the resched request and switches to the next process. */
    asm volatile("int $0x80" : : "a"(SYS_YIELD) : "memory");
}

void process_yield(void)
{
    process_block();
}

void process_kill(process_t *proc, int code)
{
    if (!proc)
        return;

    if (proc->state == PROCESS_UNUSED || proc->state == PROCESS_EXITED)
        return;

    wait_queue_cancel(proc);
    fd_close_all(proc);

    proc->state = PROCESS_EXITED;
    proc->exit_code = code;

    klog("[proc] pid %u exited (code %d)\n", proc->pid, code);

    /* Orphans have nobody to wait for them: reap them automatically. */
    for (int i = 0; i < MAX_PROCESS; i++)
    {
        if (process_table[i].parent == proc)
            process_table[i].parent = 0;
    }

    if (proc->parent)
        wait_queue_wake_all(&proc->parent->child_wq);

    if (proc == current_process)
        scheduler_request_resched();
}

int process_wait(process_t *child)
{
    int code;

    if (!child || process_waitpid((int)child->pid, &code, 0) < 0)
        return -1;

    return code;
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
    uint32_t flags = irq_save();

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

    irq_restore(flags);
}

void process_sleep(uint32_t ticks)
{
    process_t *proc = process_current();

    proc->wakeup_tick = pit_get_ticks() + ticks;
    proc->state = PROCESS_BLOCKED;

    /* scheduler_wakeup() makes it READY again on the timer IRQ. */
    process_block();
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
