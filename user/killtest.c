#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

/*
 * kill(): a child spinning in user mode, a child asleep in sys_sleep and a
 * child blocked reading stdin are all ended with EXIT_KILLED. WNOHANG and
 * kill() of kernel threads / missing pids are checked too.
 */

static int failures;

static void check(const char *what, int ok)
{
    printf("%-34s %s\n", what, ok ? "ok" : "FAIL");

    if (!ok)
        failures++;
}

static int spawn(int mode)
{
    fflush(stdout);

    int pid = fork();

    if (pid != 0)
        return pid;

    if (mode == 0)
    {
        for (volatile unsigned i = 0;; i++)
            ;
    }
    else if (mode == 1)
    {
        sleep(100);
    }
    else
    {
        char c;
        read(0, &c, 1);
    }

    exit(0);
}

int main(void)
{
    int status = 0;

    int spinner = spawn(0);
    msleep(50);
    check("WNOHANG on a running child = 0", waitpid(spinner, &status, WNOHANG) == 0);
    check("kill(spinner)", kill(spinner) == 0);
    check("spinner status = EXIT_KILLED",
          waitpid(spinner, &status, 0) == spinner && status == EXIT_KILLED);

    int sleeper = spawn(1);
    msleep(50);
    kill(sleeper);
    check("sleeper status = EXIT_KILLED",
          waitpid(sleeper, &status, 0) == sleeper && status == EXIT_KILLED);

    int reader = spawn(2);
    msleep(50);
    kill(reader);
    check("stdin reader status = EXIT_KILLED",
          waitpid(reader, &status, 0) == reader && status == EXIT_KILLED);

    check("kill(kernel thread) fails", kill(1) == -1);
    check("kill(missing pid) fails", kill(12345) == -1);
    check("kill(killed child) fails", kill(spinner) == -1);

    printf("%d failure(s)\n", failures);

    return failures;
}
