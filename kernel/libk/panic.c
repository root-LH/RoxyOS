#include <stdarg.h>
#include "../include/panic.h"
#include "../include/printk.h"
#include "../include/vga.h"

static uint32_t read_cr2(void)
{
    uint32_t v;
    asm volatile("mov %%cr2, %0" : "=r"(v));
    return v;
}

static uint32_t read_cr3(void)
{
    uint32_t v;
    asm volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

void dump_registers(registers_t *r)
{
    kprintf("EAX=%08x EBX=%08x ECX=%08x EDX=%08x\n",
            r->eax, r->ebx, r->ecx, r->edx);
    kprintf("ESI=%08x EDI=%08x EBP=%08x ESP=%08x\n",
            r->esi, r->edi, r->ebp, r->esp);
    kprintf("EIP=%08x CS=%04x EFLAGS=%08x\n",
            r->eip, r->cs, r->eflags);
    kprintf("DS=%04x ES=%04x FS=%04x GS=%04x\n",
            r->ds, r->es, r->fs, r->gs);

    /* useresp/ss are only pushed by the CPU on a privilege change */
    if ((r->cs & 3) == 3)
        kprintf("USER ESP=%08x SS=%04x\n", r->useresp, r->ss);

    kprintf("INT=%u ERR=%08x CR2=%08x CR3=%08x\n",
            r->int_no, r->err_code, read_cr2(), read_cr3());
}

static void panic_header(void)
{
    asm volatile("cli");

    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    kprintf("\n========== KERNEL PANIC ==========\n");
}

static void __attribute__((noreturn)) halt_forever(void)
{
    kprintf("System halted.\n");

    while (1)
    {
        asm volatile("cli");
        asm volatile("hlt");
    }
}

void panic(const char *fmt, ...)
{
    panic_header();

    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);

    kprintf("\n(called from %p)\n", __builtin_return_address(0));

    halt_forever();
}

void panic_regs(registers_t *r, const char *fmt, ...)
{
    panic_header();

    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);

    kprintf("\n\n");
    dump_registers(r);

    halt_forever();
}
