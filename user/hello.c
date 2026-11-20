#include <stdio.h>

static int counter = 40;        /* .data */
static char scratch[8192];      /* .bss, spans several pages */

int main(void)
{
    printf("Hello from user space!\n");

    scratch[0] = 'o';
    scratch[sizeof(scratch) - 1] = 'k';

    counter += 2;

    printf("counter = %d, bss %c%c\n",
           counter, scratch[0], scratch[sizeof(scratch) - 1]);

    return 0;
}
