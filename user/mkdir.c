#include <stdio.h>
#include <sys/stat.h>

/* mkdir dir...: creates directories. */
int main(int argc, char **argv)
{
    int status = 0;

    if (argc < 2)
    {
        fprintf(stderr, "Usage: mkdir <dir>...\n");
        return 1;
    }

    for (int i = 1; i < argc; i++)
    {
        if (mkdir(argv[i]) != 0)
        {
            fprintf(stderr, "mkdir: cannot create %s\n", argv[i]);
            status = 1;
        }
    }

    return status;
}
