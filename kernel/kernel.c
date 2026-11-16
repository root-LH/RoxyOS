#include "include/printk.h"
#include "include/vga.h"
#include "include/idt.h"
#include "include/irq.h"
#include "include/pic.h"
#include "include/pit.h"
#include "include/timer.h"
#include "include/keyboard.h"
#include "include/shell.h"
#include "include/memory.h"
#include "include/string.h"
#include "include/vfs.h"
#include "include/ata.h"
#include "include/gdt.h"
#include "include/tss.h"
#include "include/usermode.h"
#include "include/syscall.h"
#include "include/fd.h"
#include "include/process.h"
#include "include/scheduler.h"
#include "include/idle.h"
#include "include/paging.h"

__attribute__((aligned(16)))
static uint8_t user_stack[4096];
extern uint8_t stack_top;

void task1(void)
{
    while (1)
    {
        sys_write("A");
        process_sleep(300);
    }
}

void task2(void)
{
    while (1)
    {
        sys_write("B");
        process_sleep(200);
    }
}

void user_task1(void)
{
    while (1)
    {
        sys_write("B");
        sys_sleep(20);
    }
}

void user_task2(void)
{
    int cnt = 0;
    while (1)
    {
        sys_write("A");
        if (cnt++ == 1000)
            sys_exit();
    }
}

void user_mode_entry(void)
{
    while (1)
    {
        sys_write("B");
        for (volatile uint32_t i = 0; i < 1000000; i++)
            ;
    }
}

void kernel_main(void)
{
    vga_clear();

    /* CPU */
    idt_init();
    irq_install();

    /* Memory Manager */
    pmm_init(127);
    paging_init();

    /* Interrupt Controller */
    pic_remap();
    pit_init(100);

    /* Drivers */
    keyboard_init();
    ata_init();

    /* Kernel Subsystem */
    fd_init();

    /* TSS */
    tss_init();
    tss_set_kernel_stack((uint32_t)&stack_top);
    gdt_install_tss();
    tss_flush();

    scheduler_init();

    process_t *idle = process_create_kernel(idle_task);

    process_t *shell = process_create_kernel(shell_task);

    scheduler_set_idle(idle);
    scheduler_add(idle);
    scheduler_add(shell);

    process_start(shell);
}
