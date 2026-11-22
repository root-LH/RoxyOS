#include <stdio.h>
#include <unistd.h>
#include <roxy.h>

/* ps: every process (kernel threads and user programs). */
int main(void)
{
    struct procinfo p;
    int used = 0, slots = 0;
    int self = getpid();

    printf("PID   PPID  PGID  TYPE    STATE    NAME\n");

    for (int slot = 0;; slot++)
    {
        int r = procinfo(slot, &p);

        if (r < 0)
            break;

        slots++;

        if (r == 0)
            continue;

        used++;

        printf("%-4u  %-4u  %-4u  %-6s  %-7s  %s%s\n", p.pid, p.ppid, p.pgid,
               p.user ? "user" : "kernel", p.state, p.name,
               (int)p.pid == self ? " *" : "");
    }

    printf("%d/%d slots in use\n", used, slots);

    return 0;
}
