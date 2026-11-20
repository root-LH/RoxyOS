#include "ulib.h"

/*
 * Isolation test: the kernel must reject bad pointers in system calls and
 * kill this process (only) when it touches kernel memory.
 */
int main(void)
{
    int r = write(1, (const char *)0xC0010000, 16);  /* kernel address */

    print("write(kernel ptr) = ");
    print_int(r);
    print("\n");

    r = write(1, (const char *)0x00100000, 16);      /* below user space */

    print("write(low ptr)    = ");
    print_int(r);
    print("\n");

    print("reading kernel memory...\n");

    volatile uint32_t *kernel = (volatile uint32_t *)0xC0010000;
    uint32_t value = *kernel;                       /* page fault -> killed */

    print("BUG: still alive, read ");
    print_int((int)value);
    print("\n");

    return 1;
}
