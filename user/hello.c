#include "ulib.h"

static int counter = 40;        /* .data */
static char scratch[8192];      /* .bss, spans several pages */

int main(void)
{
    print("Hello from user space!\n");

    scratch[0] = 'o';
    scratch[sizeof(scratch) - 1] = 'k';

    counter += 2;

    print("counter = ");
    print_int(counter);
    print(", bss ");

    char ok[3] = { scratch[0], scratch[sizeof(scratch) - 1], '\0' };
    print(ok);
    print("\n");

    return 0;
}
