#include <stdio.h>
#include <string.h>

/* echo [-n] [text...]: prints its arguments separated by spaces. */
int main(int argc, char **argv)
{
    int newline = 1, i = 1;

    if (argc > 1 && strcmp(argv[1], "-n") == 0)
    {
        newline = 0;
        i++;
    }

    for (; i < argc; i++)
        printf("%s%s", argv[i], i + 1 < argc ? " " : "");

    if (newline)
        putchar('\n');

    return 0;
}
