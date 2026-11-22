#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

/* wc [file...]: lines, words and bytes (stdin without files). */
static unsigned int total[3];

static int count(int fd, const char *name)
{
    char buf[1024];
    unsigned int lines = 0, words = 0, bytes = 0;
    int in_word = 0, n;

    while ((n = read(fd, buf, sizeof(buf))) > 0)
    {
        bytes += (unsigned int)n;

        for (int i = 0; i < n; i++)
        {
            char c = buf[i];
            int space = c == ' ' || c == '\t' || c == '\n' || c == '\r';

            if (c == '\n')
                lines++;

            if (!space && !in_word)
                words++;

            in_word = !space;
        }
    }

    printf("%7u %7u %7u %s\n", lines, words, bytes, name);

    total[0] += lines;
    total[1] += words;
    total[2] += bytes;

    return n < 0;
}

int main(int argc, char **argv)
{
    int status = 0;

    if (argc < 2)
        return count(STDIN_FILENO, "");

    for (int i = 1; i < argc; i++)
    {
        int fd = open(argv[i], O_RDONLY);

        if (fd < 0)
        {
            fprintf(stderr, "wc: %s: cannot open\n", argv[i]);
            status = 1;
            continue;
        }

        status |= count(fd, argv[i]);
        close(fd);
    }

    if (argc > 2)
        printf("%7u %7u %7u total\n", total[0], total[1], total[2]);

    return status;
}
