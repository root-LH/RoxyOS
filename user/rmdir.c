#include <stdio.h>
#include <unistd.h>

/* rmdir dir...: removes empty directories. */
int main(int argc, char **argv)
{
    int status = 0;

    if (argc < 2)
    {
        fprintf(stderr, "Usage: rmdir <dir>...\n");
        return 1;
    }

    for (int i = 1; i < argc; i++)
    {
        if (rmdir(argv[i]) != 0)
        {
            fprintf(stderr, "rmdir: cannot remove %s (missing, not empty, or in use)\n", argv[i]);
            status = 1;
        }
    }

    return status;
}
