#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * Exercises sleeping (preemption while blocked) and exit codes.
 * Usage: count [n]   (default 3; the exit code is n)
 */
int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 3;

    for (int i = 1; i <= n; i++)
    {
        printf("count %d (%u ms)\n", i, uptime_ms());
        msleep(200);
    }

    return n;
}
