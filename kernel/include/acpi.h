#ifndef ACPI_H
#define ACPI_H

#include "types.h"

/*
 * Just enough ACPI to power off: RSDP -> RSDT -> FADT (PM1 control
 * ports) and the \_S5_ object in the DSDT (sleep type values for S5).
 */
void acpi_init(void);

/* 1 if acpi_init() found everything acpi_poweroff() needs. */
int acpi_available(void);

/*
 * Enters S5 (soft off). Falls back to the QEMU/Bochs/VirtualBox shutdown
 * ports when ACPI is missing. Returns only if the machine is still on.
 */
void acpi_poweroff(void);

#endif
