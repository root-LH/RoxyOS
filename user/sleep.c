#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* sleep seconds: waits (handy for trying out jobs: sleep 5 &). */
int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: sleep <seconds>\n");
        return 1;
    }

    sleep((unsigned int)atoi(argv[1]));

    return 0;
}
