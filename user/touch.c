#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

/* touch file...: creates missing files (existing ones are left alone). */
int main(int argc, char **argv)
{
    int status = 0;

    if (argc < 2)
    {
        fprintf(stderr, "Usage: touch <file>...\n");
        return 1;
    }

    for (int i = 1; i < argc; i++)
    {
        int fd = open(argv[i], O_WRONLY | O_CREAT);

        if (fd < 0)
        {
            fprintf(stderr, "touch: cannot create %s\n", argv[i]);
            status = 1;
            continue;
        }

        close(fd);
    }

    return status;
}
