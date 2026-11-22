#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <roxy.h>

/* uptime: current time, time since boot, number of processes, boot time. */
int main(void)
{
    unsigned int secs = uptime_ms() / 1000;
    unsigned int days = secs / 86400;
    time_t now = time(NULL);
    time_t boot = now - (time_t)secs;
    struct procinfo p;
    int procs = 0;

    for (int slot = 0;; slot++)
    {
        int r = procinfo(slot, &p);

        if (r < 0)
            break;

        procs += r;
    }

    char buf[32];

    strftime(buf, sizeof(buf), "%H:%M:%S", gmtime(&now));
    printf("%s up ", buf);

    if (days)
        printf("%u day%s, ", days, days == 1 ? "" : "s");

    printf("%u:%02u:%02u, %d processes\n", secs / 3600 % 24, secs / 60 % 60,
           secs % 60, procs);

    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gmtime(&boot));
    printf("booted %s\n", buf);

    return 0;
}
