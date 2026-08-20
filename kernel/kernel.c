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
        sys_write("C");
        sys_sleep(400);
        sys_exit(0);
    }
}

void user_task2(void)
{
    while (1)
    {
        sys_write("D");
        sys_sleep(100);
    }
}

void user_mode_entry(void)
{
    while (1)
    {
        sys_write("TEST");
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

    process_t *idle = process_create_kernel(idle_task);
/*
    process_t *p1 = process_create_kernel(task1);
    process_t *p2 = process_create_kernel(task2);

    process_t *user1 = process_create_user(user_task1);
    process_t *user2 = process_create_user(user_task2);
*/
    //process_t *user = process_create_user(user_mode_entry);

    scheduler_set_idle(idle);
    scheduler_add(idle);
/*
    scheduler_add(p1);
    scheduler_add(p2);
    //scheduler_add(user);

    scheduler_add(user1);
    scheduler_add(user2);
*/
    asm volatile("sti");
/*
    process_switch(p1);
*/
    //process_enter_user(user1);

    /*
    uint32_t user_stack_top = (uint32_t)(user_stack + sizeof(user_stack));

    switch_to_user_mode(user_stack_top);

    printk("ERROR: Returned from user mode!\n");
    */

    while (1)
    {
        asm volatile("hlt");
    }
}