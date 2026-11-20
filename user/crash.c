#include <stdio.h>
#include <unistd.h>

/*
 * Isolation test: the kernel must reject bad pointers in system calls and
 * kill this process (only) when it touches kernel memory.
 */
int main(void)
{
    printf("write(kernel ptr) = %d\n", (int)write(1, (const char *)0xC0010000, 16));
    printf("write(low ptr)    = %d\n", (int)write(1, (const char *)0x00100000, 16));
    printf("read(into code)   = %d\n", (int)read(0, (char *)main, 4));

    printf("reading kernel memory...\n");

    volatile unsigned int *kernel = (volatile unsigned int *)0xC0010000;
    unsigned int value = *kernel;                   /* page fault -> killed */

    printf("BUG: still alive, read %u\n", value);

    return 1;
}
