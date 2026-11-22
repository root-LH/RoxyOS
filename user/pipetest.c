#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

/*
 * pipe / dup / dup2: bulk transfer bigger than the pipe buffer, EOF and
 * broken pipes, capturing another program's stdout through exec, and
 * killing a writer blocked on a full pipe.
 */

static int failures;

static void check(const char *what, int ok)
{
    printf("%-40s %s\n", what, ok ? "ok" : "FAIL");

    if (!ok)
        failures++;
}

#define TOTAL 20000     /* about 5x the 4 KB pipe buffer */

static void bulk(void)
{
    int fds[2];

    check("pipe()", pipe(fds) == 0);

    struct stat st;
    check("fstat: S_IFIFO", fstat(fds[0], &st) == 0 && S_ISFIFO(st.st_mode));

    fflush(stdout);

    int pid = fork();

    if (pid == 0)
    {
        close(fds[0]);

        char buf[1000];

        for (int sent = 0; sent < TOTAL; sent += sizeof(buf))
        {
            for (unsigned i = 0; i < sizeof(buf); i++)
                buf[i] = (char)((sent + i) % 251);

            write(fds[1], buf, sizeof(buf));
        }

        exit(0);            /* closing the last write end -> EOF */
    }

    close(fds[1]);

    char buf[700];
    int total = 0, ok = 1, n;

    while ((n = read(fds[0], buf, sizeof(buf))) > 0)
    {
        for (int i = 0; i < n; i++)
            ok &= buf[i] == (char)((total + i) % 251);

        total += n;
    }

    check("20000 bytes through the pipe, in order", ok && total == TOTAL);
    check("read after the writer exits = 0 (EOF)", n == 0);

    int status;
    waitpid(pid, &status, 0);
    close(fds[0]);
}

static void broken(void)
{
    int fds[2];

    pipe(fds);
    close(fds[0]);
    fflush(stdout);

    /* Like SIGPIPE: the writer ends (EXIT_PIPE) instead of seeing -1. */
    int pid = fork();

    if (pid == 0)
    {
        write(fds[1], "x", 1);
        _exit(0);
    }

    close(fds[1]);

    int status = 0;

    waitpid(pid, &status, 0);
    check("write with no reader ends the writer (EXIT_PIPE)", status == EXIT_PIPE);
}

static void capture(void)
{
    int fds[2];

    pipe(fds);
    fflush(stdout);

    int pid = fork();

    if (pid == 0)
    {
        /* The child's stdout becomes the pipe; args never knows. */
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]);
        close(fds[1]);

        char *argv[] = { "args", "piped", NULL };
        exec("args", argv);
        exit(99);
    }

    close(fds[1]);

    char out[512];
    int len = 0, n;

    while ((n = read(fds[0], out + len, sizeof(out) - 1 - len)) > 0)
        len += n;

    out[len] = '\0';
    close(fds[0]);

    int status;
    waitpid(pid, &status, 0);

    check("dup2 + exec: output captured", strstr(out, "argv[1] = \"piped\"") != NULL);
    check("captured program exited 0", status == 0);
}

static void dup_test(void)
{
    int fd = dup(STDOUT_FILENO);

    check("dup(1) returns a new fd", fd > STDERR_FILENO);
    check("writing to the copy works", write(fd, "", 0) == 0 && write(fd, " ", 1) == 1);
    close(fd);

    check("dup2 to a bad fd fails", dup2(STDOUT_FILENO, 99) == -1);
    check("dup of a closed fd fails", dup(fd) == -1);
}

static void kill_blocked_writer(void)
{
    int fds[2];

    pipe(fds);
    fflush(stdout);

    int pid = fork();

    if (pid == 0)
    {
        char buf[1024];

        memset(buf, 'z', sizeof(buf));

        for (;;)
            write(fds[1], buf, sizeof(buf));    /* fills up, then blocks */
    }

    msleep(100);
    kill(pid);

    int status;
    check("writer blocked on a full pipe killed",
          waitpid(pid, &status, 0) == pid && status == EXIT_KILLED);

    close(fds[0]);
    close(fds[1]);
}

int main(void)
{
    bulk();
    broken();
    capture();
    dup_test();
    printf("\n");
    kill_blocked_writer();

    printf("%d failure(s)\n", failures);

    return failures;
}
