#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

/*
 * fork / waitpid / exec:
 *  - children get a private copy of memory and exit with distinct codes
 *  - waitpid collects specific children, then any child, then fails
 *  - a child exec()s "args" with an argument vector
 */

static int shared = 100;

int main(void)
{
    int parent = getpid();
    int pids[3];

    printf("parent pid %d\n", parent);

    for (int i = 0; i < 3; i++)
    {
        fflush(stdout);     /* don't duplicate buffered output into the child */

        int pid = fork();

        if (pid < 0)
        {
            printf("fork failed\n");
            return 1;
        }

        if (pid == 0)
        {
            shared += i + 1;
            printf("  child %d (pid %d): shared = %d\n", i, getpid(), shared);
            msleep(100 * (3 - i));      /* exit in reverse order */
            exit(10 + i);
        }

        pids[i] = pid;
    }

    int status;

    /* Wait for the first child by pid even though it exits last. */
    int got = waitpid(pids[0], &status, 0);
    printf("waitpid(%d) -> %d, status %d\n", pids[0], got, status);

    for (int i = 0; i < 2; i++)
    {
        got = wait(&status);
        printf("wait() -> %d, status %d\n", got, status);
    }

    printf("wait() with no children -> %d\n", wait(&status));
    printf("parent shared = %d (unchanged)\n", shared);

    fflush(stdout);

    int pid = fork();

    if (pid == 0)
    {
        char *argv[] = { "args", "from", "exec", NULL };

        exec("args", argv);
        printf("exec failed\n");
        exit(99);
    }

    waitpid(pid, &status, 0);
    printf("exec child %d -> status %d\n", pid, status);

    printf("exec(missing) -> %d\n", exec("no-such-program", NULL));

    return 0;
}
