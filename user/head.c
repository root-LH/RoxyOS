#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

/* head [-n lines] [file]: the first lines (default 10) of a file or stdin. */
int main(int argc, char **argv)
{
    long lines = 10;
    const char *path = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            lines = strtol(argv[++i], NULL, 10);
        else
            path = argv[i];
    }

    int fd = path ? open(path, O_RDONLY) : STDIN_FILENO;

    if (fd < 0)
    {
        fprintf(stderr, "head: %s: cannot open\n", path);
        return 1;
    }

    char buf[512];
    int n;

    while (lines > 0 && (n = read(fd, buf, sizeof(buf))) > 0)
    {
        int end = 0;

        while (end < n && lines > 0)
        {
            if (buf[end++] == '\n')
                lines--;
        }

        write(STDOUT_FILENO, buf, (size_t)end);
    }

    if (path)
        close(fd);

    return 0;
}
