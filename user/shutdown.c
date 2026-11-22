#include <stdio.h>
#include <roxy.h>

/* shutdown: powers the machine off through ACPI (S5). */
int main(void)
{
    reboot(REBOOT_POWEROFF);

    /* The kernel already said why. */
    return 1;
}
