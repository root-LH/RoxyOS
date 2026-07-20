#include "include/printk.h"
#include "include/vga.h"
#include "include/idt.h"
#include "include/irq.h"
#include "include/pic.h"
#include "include/pit.h"
#include "include/timer.h"
#include "include/keyboard.h"
#include "include/shell.h"

static void trigger_divide_error(void)
{
    __asm__ volatile(
        "xor %%edx, %%edx\n\t"
        "mov $1, %%eax\n\t"
        "xor %%ecx, %%ecx\n\t"
        "div %%ecx"
        :
        :
        : "eax", "ecx", "edx");
}

void kernel_main(void)
{
    vga_clear();

    idt_init();

    irq_install();

    pic_remap();
    pit_init(100);

    keyboard_init();

    asm volatile("sti");

    shell_init();

    uint32_t last = 0;

    while (1){
        asm volatile("hlt");
    }
}