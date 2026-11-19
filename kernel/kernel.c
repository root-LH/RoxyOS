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
#include "include/syscall.h"
#include "include/fd.h"
#include "include/process.h"
#include "include/scheduler.h"
#include "include/idle.h"
#include "include/paging.h"
#include "include/serial.h"

extern uint8_t stack_top;

void kernel_main(void)
{
    vga_clear();

    /* Serial log (COM1) first, so everything after this is captured */
    if (serial_init() == 0)
        klog("\n[boot] RoxyOS serial log on COM1\n");

    /* CPU */
    gdt_init();
    idt_init();
    irq_install();

    /* Memory Manager */
    pmm_init();
    paging_init();
    heap_init();

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

    klog("[boot] init done, starting scheduler\n");

    process_t *idle = process_create_kernel(idle_task);

    process_t *shell = process_create_kernel(shell_task);

    scheduler_set_idle(idle);
    scheduler_add(idle);
    scheduler_add(shell);

    process_start(shell);
}
