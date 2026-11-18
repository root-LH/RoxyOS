#include "../../include/panic.h"
#include "../../include/isr.h"
#include "../../include/printk.h"
#include "../../include/process.h"
#include "../../include/scheduler.h"

#define EXC_PAGE_FAULT 14

/* Page fault error code bits */
#define PF_PRESENT  0x01  /* 0: non-present page, 1: protection violation */
#define PF_WRITE    0x02  /* 0: read, 1: write                            */
#define PF_USER     0x04  /* 0: supervisor, 1: user mode                  */
#define PF_RSVD     0x08  /* reserved bit set in a paging entry           */
#define PF_IFETCH   0x10  /* instruction fetch                            */

static const char *exception_messages[32] =
{
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",

    "Double Fault",
    "Coprocessor Segment Overrun",

    "Invalid TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",

    "Reserved",

    "x87 Floating Point",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating Point",

    "Virtualization",
    "Control Protection",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection",
    "VMM Communication",
    "Security Exception",
    "Reserved"
};

static uint32_t read_cr2(void)
{
    uint32_t v;
    asm volatile("mov %%cr2, %0" : "=r"(v));
    return v;
}

static int from_user_mode(registers_t *r)
{
    return (r->cs & 3) == 3;
}

/*
 * Kills the current user process after a fault and returns the trap frame
 * of the next process to run.
 */
static registers_t *kill_current_user(registers_t *r)
{
    process_t *proc = process_current();

    kprintf("[fault] pid %u killed\n", proc->pid);

    process_kill(proc, -1);

    registers_t *next = process_reschedule_from_trap(r);
    scheduler_clear_resched();

    return next;
}

static registers_t *page_fault_handler(registers_t *r)
{
    uint32_t addr = read_cr2();
    uint32_t err = r->err_code;

    const char *cause  = (err & PF_PRESENT) ? "protection violation" : "page not present";
    const char *access = (err & PF_IFETCH) ? "exec" : (err & PF_WRITE) ? "write" : "read";
    const char *mode   = (err & PF_USER) ? "user" : "kernel";

    process_t *proc = process_current();

    if (from_user_mode(r) && proc && proc->type == PROCESS_USER)
    {
        kprintf("\n[page fault] pid %u: %s %s at %p (%s%s), eip=%p\n",
                proc->pid, mode, access, (void *)addr, cause,
                (err & PF_RSVD) ? ", reserved bit" : "",
                (void *)r->eip);

        return kill_current_user(r);
    }

    panic_regs(r, "Page Fault: %s %s at %p (%s%s)\npid=%u",
               mode, access, (void *)addr, cause,
               (err & PF_RSVD) ? ", reserved bit" : "",
               proc ? proc->pid : 0);
}

registers_t *isr_handler(registers_t *r)
{
    if (r->int_no == EXC_PAGE_FAULT)
        return page_fault_handler(r);

    const char *name = r->int_no < 32 ? exception_messages[r->int_no] : "Unknown";
    process_t *proc = process_current();

    /* Any other CPU exception raised by a user process kills only that process. */
    if (from_user_mode(r) && proc && proc->type == PROCESS_USER)
    {
        kprintf("\n[exception] pid %u: %s (err=%08x) at eip=%p\n",
                proc->pid, name, r->err_code, (void *)r->eip);

        return kill_current_user(r);
    }

    panic_regs(r, "%s", name);
}
