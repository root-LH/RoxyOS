#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

/*
 * hexdump [-n count] [-s skip] [file]: offset, 16 bytes in hex and as
 * text per line (stdin without a file). A run of identical lines is
 * shown once, followed by '*'.
 */
static void usage(void)
{
    fprintf(stderr, "Usage: hexdump [-n count] [-s skip] [file]\n");
    exit(1);
}

static void print_line(unsigned long offset, const unsigned char *b, int n)
{
    printf("%08lx  ", offset);

    for (int i = 0; i < 16; i++)
    {
        if (i < n)
            printf("%02x ", b[i]);
        else
            printf("   ");

        if (i == 7)
            putchar(' ');
    }

    printf(" |");

    for (int i = 0; i < n; i++)
        putchar(b[i] >= 0x20 && b[i] < 0x7F ? b[i] : '.');

    printf("|\n");
}

/* Fills buf with up to len bytes (short only at end of input). */
static int read_full(int fd, unsigned char *buf, int len)
{
    int got = 0;

    while (got < len)
    {
        int n = read(fd, buf + got, (size_t)(len - got));

        if (n <= 0)
            break;

        got += n;
    }

    return got;
}

int main(int argc, char **argv)
{
    long limit = -1, skip = 0;
    const char *path = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            limit = strtol(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)
            skip = strtol(argv[++i], NULL, 0);
        else if (argv[i][0] == '-')
            usage();
        else if (!path)
            path = argv[i];
        else
            usage();
    }

    int fd = path ? open(path, O_RDONLY) : STDIN_FILENO;

    if (fd < 0)
    {
        fprintf(stderr, "hexdump: %s: cannot open\n", path);
        return 1;
    }

    unsigned char line[16], prev[16];
    unsigned long offset = 0;
    int have_prev = 0, starred = 0;

    /* Skip: seek when possible, otherwise read and drop. */
    if (skip > 0)
    {
        if (lseek(fd, skip, SEEK_SET) == skip)
        {
            offset = (unsigned long)skip;
        }
        else
        {
            while (offset < (unsigned long)skip)
            {
                long want = skip - (long)offset;
                int n = read_full(fd, line, want < 16 ? (int)want : 16);

                if (n <= 0)
                    break;

                offset += (unsigned long)n;
            }
        }
    }

    for (;;)
    {
        int want = 16;

        if (limit >= 0 && limit - (long)(offset - (unsigned long)skip) < want)
            want = (int)(limit - (long)(offset - (unsigned long)skip));

        if (want <= 0)
            break;

        int n = read_full(fd, line, want);

        if (n <= 0)
            break;

        if (have_prev && n == 16 && memcmp(line, prev, 16) == 0)
        {
            if (!starred)
                printf("*\n");

            starred = 1;
        }
        else
        {
            print_line(offset, line, n);
            starred = 0;
        }

        memcpy(prev, line, 16);
        have_prev = n == 16;
        offset += (unsigned long)n;

        if (n < want)
            break;
    }

    printf("%08lx\n", offset);

    if (path)
        close(fd);

    return 0;
}
