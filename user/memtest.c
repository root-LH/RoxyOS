#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/* malloc / free / realloc on top of sbrk, and heap copies across fork. */

static int failures;

static void check(const char *what, int ok)
{
    printf("%-32s %s\n", what, ok ? "ok" : "FAIL");

    if (!ok)
        failures++;
}

int main(void)
{
    char *start = sbrk(0);

    int *small[64];

    for (int i = 0; i < 64; i++)
    {
        small[i] = malloc(sizeof(int) * (i + 1));
        small[i][i] = i;
    }

    int ok = 1;

    for (int i = 0; i < 64; i++)
        ok &= small[i][i] == i;

    check("64 small blocks keep their data", ok);

    for (int i = 0; i < 64; i += 2)
        free(small[i]);

    void *reused = malloc(16);
    check("freed space is reused", (char *)reused < (char *)small[63]);

    char *big = malloc(100 * 1024);
    check("100 KB block", big != NULL);
    memset(big, 0xAB, 100 * 1024);
    check("break moved past the big block", (char *)sbrk(0) >= big + 100 * 1024);

    char *text = strdup("hello heap");
    text = realloc(text, 4096);
    check("realloc keeps contents", strcmp(text, "hello heap") == 0);

    fflush(stdout);

    int pid = fork();

    if (pid == 0)
    {
        /* The child's heap is a copy: changing it must not affect the parent. */
        strcpy(text, "child was here");
        big[0] = 0;
        exit(strcmp(text, "child was here") == 0 ? 0 : 1);
    }

    int status;
    waitpid(pid, &status, 0);
    check("child modified its heap copy", status == 0);
    check("parent heap unchanged", strcmp(text, "hello heap") == 0 && (unsigned char)big[0] == 0xAB);

    for (int i = 1; i < 64; i += 2)
        free(small[i]);

    free(reused);
    free(big);
    free(text);

    char *before = sbrk(0);
    check("sbrk(-4096) shrinks the heap", sbrk(-4096) == before && (char *)sbrk(0) == before - 4096);
    sbrk(4096);

    check("huge sbrk fails", sbrk(0x7FFFFFFF) == (void *)-1);
    check("malloc(512 MB) returns NULL", malloc(512u * 1024 * 1024) == NULL);

    printf("heap grew from %p to %p\n", (void *)start, sbrk(0));
    printf("%d failure(s)\n", failures);

    return failures;
}
