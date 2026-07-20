#include "../../include/panic.h"
#include "../../include/isr.h"
#include "../../include/printk.h"

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

void isr_handler(registers_t *r)
{
    printk("\n========== KERNEL PANIC ==========\n\n");

    printk(exception_messages[r->int_no]);

    printk("\n\nEAX = ");
    printk_hex(r->eax);

    printk("\nEIP = ");
    printk_hex(r->eip);

    printk("\nCS  = ");
    printk_hex(r->cs);

    printk("\nEFLAGS = ");
    printk_hex(r->eflags);

    panic("");
}