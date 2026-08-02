#include "../../include/tss.h"
#include "../../include/string.h"

tss_entry_t tss;

void tss_set_kernel_stack(uint32_t stack)
{
    tss.esp0 = stack;
}

void tss_init(void)
{
    memset(&tss, 0, sizeof(tss));

    tss.ss0 = 0x10;          // Kernel Data
    tss.esp0 = 0;

    tss.iomap_base = sizeof(tss);
}