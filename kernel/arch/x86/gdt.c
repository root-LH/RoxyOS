#include "../../include/gdt.h"
#include "../../include/tss.h"

#define TSS_GDT_INDEX 5

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