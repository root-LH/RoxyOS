#include <stdio.h>
#include <roxy.h>

/* reboot: restarts the machine (the disk cache is write-through). */
int main(void)
{
    reboot(REBOOT_RESTART);

    fprintf(stderr, "reboot: failed\n");
    return 1;
}
