#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* kill pid...: ends user processes (exit code 137). */
int main(int argc, char **argv)
{
    int status = 0;

    if (argc < 2)
    {
        fprintf(stderr, "Usage: kill <pid>...\n");
        return 1;
    }

    for (int i = 1; i < argc; i++)
    {
        char *end;
        long pid = strtol(argv[i], &end, 10);

        if (*end != '\0' || pid <= 0)
        {
            fprintf(stderr, "kill: %s: not a pid\n", argv[i]);
            status = 1;
        }
        else if (kill((pid_t)pid) != 0)
        {
            fprintf(stderr, "kill: no user process %ld\n", pid);
            status = 1;
        }
    }

    return status;
}
