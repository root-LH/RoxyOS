#include <stdio.h>
#include <string.h>
#include <roxy.h>

/* lspci [-v]: PCI functions found at boot (-v adds IRQ and BARs). */
int main(int argc, char **argv)
{
    int verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    struct pciinfo d;
    int i = 0;

    for (; pciinfo(i, &d) == 0; i++)
    {
        printf("%02x:%02x.%u %s: %04x:%04x", d.bus, d.slot, d.func,
               d.class_name, d.vendor_id, d.device_id);

        if (d.vendor_name[0])
            printf(" (%s)", d.vendor_name);

        printf("\n");

        if (!verbose)
            continue;

        printf("        class %02x.%02x.%02x rev %02x", d.class_code, d.subclass,
               d.prog_if, d.revision);

        if (d.irq_pin)
            printf(", INT%c IRQ %u", 'A' + d.irq_pin - 1, d.irq_line);

        printf("\n");

        for (int b = 0; b < 6 && d.header_type == 0; b++)
        {
            unsigned int bar = d.bar[b];

            if (!bar)
                continue;

            if (bar & 1)
                printf("        BAR%d: I/O at 0x%x\n", b, bar & ~3u);
            else
                printf("        BAR%d: memory at 0x%x\n", b, bar & ~15u);
        }
    }

    if (i == 0)
        printf("lspci: no PCI devices\n");

    return 0;
}
