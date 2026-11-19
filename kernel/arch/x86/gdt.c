#include "../../include/gdt.h"
#include "../../include/tss.h"

#define TSS_GDT_INDEX 5
#define GDT_ENTRIES   6

/*
 * Same layout as boot/gdt.inc, but inside the kernel image so the CPU reads
 * it through a higher-half address instead of the Stage2 copy in low memory.
 */
static gdt_entry_t gdt[GDT_ENTRIES] __attribute__((aligned(8)));
static gdtr_t gdtr;

static void gdt_set_entry(int i, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t flags)
{
    gdt[i].limit_low   = limit & 0xFFFF;
    gdt[i].base_low    = base & 0xFFFF;
    gdt[i].base_mid    = (base >> 16) & 0xFF;
    gdt[i].access      = access;
    gdt[i].granularity = (flags & 0xF0) | ((limit >> 16) & 0x0F);
    gdt[i].base_high   = (base >> 24) & 0xFF;
}

void gdt_init(void)
{
    gdt_set_entry(0, 0, 0, 0, 0);
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xC0);   /* 0x08 kernel code */
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xC0);   /* 0x10 kernel data */
    gdt_set_entry(3, 0, 0xFFFFF, 0xFA, 0xC0);   /* 0x18 user code (DPL3) */
    gdt_set_entry(4, 0, 0xFFFFF, 0xF2, 0xC0);   /* 0x20 user data (DPL3) */
    gdt_set_entry(5, 0, 0, 0, 0);               /* 0x28 TSS (gdt_install_tss) */

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base  = (uint32_t)gdt;

    __asm__ volatile(
        "lgdt %0\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        "ljmp $0x08, $1f\n"
        "1:\n\t"
        : : "m"(gdtr) : "eax", "memory");
}

void gdt_get_descriptor(gdtr_t *gdtr)
{
    __asm__ volatile("sgdt %0" : "=m"(*gdtr));
}

extern tss_entry_t tss;

void gdt_install_tss(void)
{
    gdtr_t gdtr;

    gdt_get_descriptor(&gdtr);

    gdt_entry_t *gdt = (gdt_entry_t *)gdtr.base;

    uint32_t base  = (uint32_t)&tss;
    uint32_t limit = sizeof(tss_entry_t) - 1;

    gdt[TSS_GDT_INDEX].limit_low = limit & 0xFFFF;

    gdt[TSS_GDT_INDEX].base_low = base & 0xFFFF;
    gdt[TSS_GDT_INDEX].base_mid = (base >> 16) & 0xFF;
    gdt[TSS_GDT_INDEX].base_high = (base >> 24) & 0xFF;

    gdt[TSS_GDT_INDEX].access = 0x89;

    gdt[TSS_GDT_INDEX].granularity = (limit >> 16) & 0x0F;
}