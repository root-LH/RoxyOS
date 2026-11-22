#include <stdio.h>
#include <unistd.h>

/* rm file...: removes files (not directories, not files still open). */
int main(int argc, char **argv)
{
    int status = 0;

    if (argc < 2)
    {
        fprintf(stderr, "Usage: rm <file>...\n");
        return 1;
    }

    for (int i = 1; i < argc; i++)
    {
        if (unlink(argv[i]) != 0)
        {
            fprintf(stderr, "rm: cannot remove %s (missing, a directory, or open)\n", argv[i]);
            status = 1;
        }
    }

    return status;
}
