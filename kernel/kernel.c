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

__attribute__((aligned(16)))
static uint8_t user_stack[4096];
extern uint8_t stack_top;

void task1(void)
{
    while (1)
    {
        printk("A");
        process_sleep(500);
    }
}

void task2(void)
{
    while (1)
    {
        printk("B");
        process_sleep(100);
    }
}

void user_mode_entry(void)
{
    while (1)
    {
        
    }
}

void kernel_main(void)
{
    vga_clear();

    /* CPU */
    idt_init();
    irq_install();

    /* Interrupt Controller */
    pic_remap();
    pit_init(100);

    /* Drivers */
    keyboard_init();
    ata_init();

    /* Kernel Subsystem */
    fd_init();
    shell_init();
    
    keyboard_set_input_start();

    /* TSS */
    tss_init();
    tss_set_kernel_stack((uint32_t)&stack_top);
    gdt_install_tss();
    tss_flush();

    scheduler_init();
    /*
    process_t *idle = process_create_kernel(idle_task);

    process_t *p1 = process_create_kernel(task1);
    process_t *p2 = process_create_kernel(task2);

    scheduler_set_idle(idle);

    scheduler_add(p1);
    scheduler_add(p2);

    asm volatile("sti");

    process_switch(p1);
    */

    /*
    uint32_t user_stack_top = (uint32_t)(user_stack + sizeof(user_stack));

    switch_to_user_mode(user_stack_top);

    printk("ERROR: Returned from user mode!\n");
    */

    asm volatile("sti");

    while (1)
    {
        asm volatile("hlt");
    }
}