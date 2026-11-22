#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/* cat [file...]: copies the files (or stdin, also for "-") to stdout. */
static int copy(int fd, const char *name)
{
    char buf[4096];
    int n;

    while ((n = read(fd, buf, sizeof(buf))) > 0)
    {
        for (int done = 0; done < n; )
        {
            int w = write(STDOUT_FILENO, buf + done, (size_t)(n - done));

            if (w <= 0)
                return 1;       /* reader gone (closed pipe) */

            done += w;
        }
    }

    if (n < 0)
    {
        fprintf(stderr, "cat: %s: read error\n", name);
        return 1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    int status = 0;

    if (argc < 2)
        return copy(STDIN_FILENO, "stdin");

    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-' && argv[i][1] == '\0')
        {
            status |= copy(STDIN_FILENO, "stdin");
            continue;
        }

        struct stat st;
        int fd = open(argv[i], O_RDONLY);

        if (fd < 0 || (fstat(fd, &st) == 0 && st.st_mode == S_IFDIR))
        {
            fprintf(stderr, "cat: %s: cannot read\n", argv[i]);

            if (fd >= 0)
                close(fd);

            status = 1;
            continue;
        }

        status |= copy(fd, argv[i]);
        close(fd);
    }

    return status;
}
