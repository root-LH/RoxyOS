#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

/*
 * grep [-i] [-v] [-n] [-c] text [file...]: lines containing text (a plain
 * substring, no regular expressions). Exit code 0 if a line matched.
 */
static int ignore_case, invert, numbers, count_only;
static const char *pattern;

static int contains(const char *line)
{
    size_t plen = strlen(pattern);

    for (const char *p = line; *p; p++)
    {
        size_t i = 0;

        while (i < plen && p[i] &&
               (ignore_case ? tolower((unsigned char)p[i]) == tolower((unsigned char)pattern[i])
                            : p[i] == pattern[i]))
            i++;

        if (i == plen)
            return 1;
    }

    return plen == 0;
}

/* Returns how many lines matched. */
static int search(int fd, const char *name, int show_name)
{
    char buf[512], line[512];
    int len = 0, n, matches = 0, lineno = 0;

    for (;;)
    {
        n = read(fd, buf, sizeof(buf));

        for (int i = 0; i < n || (n <= 0 && len > 0 && i == 0); i++)
        {
            char c = n > 0 ? buf[i] : '\n';

            if (c != '\n' && len < (int)sizeof(line) - 1)
            {
                line[len++] = c;
                continue;
            }

            if (c != '\n')
                continue;       /* overlong line: the rest is dropped */

            line[len] = '\0';
            len = 0;
            lineno++;

            if (contains(line) == invert)
                continue;

            matches++;

            if (count_only)
                continue;

            if (show_name)
                printf("%s:", name);

            if (numbers)
                printf("%d:", lineno);

            printf("%s\n", line);
        }

        if (n <= 0)
            break;
    }

    if (count_only)
    {
        if (show_name)
            printf("%s:", name);

        printf("%d\n", matches);
    }

    return matches;
}

int main(int argc, char **argv)
{
    int i = 1;

    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++)
    {
        for (const char *f = argv[i] + 1; *f; f++)
        {
            switch (*f)
            {
                case 'i': ignore_case = 1; break;
                case 'v': invert = 1; break;
                case 'n': numbers = 1; break;
                case 'c': count_only = 1; break;
                default:
                    fprintf(stderr, "grep: unknown option -%c\n", *f);
                    return 2;
            }
        }
    }

    if (i >= argc)
    {
        fprintf(stderr, "Usage: grep [-i] [-v] [-n] [-c] <text> [file...]\n");
        return 2;
    }

    pattern = argv[i++];

    int matches = 0, status = 0;

    if (i >= argc)
        matches = search(STDIN_FILENO, "", 0);

    for (int first = i; i < argc; i++)
    {
        int fd = open(argv[i], O_RDONLY);

        if (fd < 0)
        {
            fprintf(stderr, "grep: %s: cannot open\n", argv[i]);
            status = 2;
            continue;
        }

        matches += search(fd, argv[i], argc - first > 1);
        close(fd);
    }

    return status ? status : matches ? 0 : 1;
}
