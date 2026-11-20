#include <stdio.h>
#include <unistd.h>

/* Prints its arguments: run args a b c */
int main(int argc, char **argv)
{
    printf("pid %d, argc = %d\n", getpid(), argc);

    for (int i = 0; i < argc; i++)
        printf("  argv[%d] = \"%s\"\n", i, argv[i]);

    printf("  argv[%d] = %p\n", argc, (void *)argv[argc]);

    return 0;
}
