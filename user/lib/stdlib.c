#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>

void exit(int code)
{
    fflush(stdout);
    fflush(stderr);

    _exit(code);
}

long strtol(const char *s, char **end, int base)
{
    while (isspace(*s))
        s++;

    int negative = 0;

    if (*s == '-' || *s == '+')
        negative = *s++ == '-';

    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        s += 2;
        base = 16;
    }
    else if (base == 0)
    {
        base = s[0] == '0' ? 8 : 10;
    }

    long value = 0;

    for (;; s++)
    {
        int digit;

        if (isdigit(*s))
            digit = *s - '0';
        else if (isalpha(*s))
            digit = tolower(*s) - 'a' + 10;
        else
            break;

        if (digit >= base)
            break;

        value = value * base + digit;
    }

    if (end)
        *end = (char *)s;

    return negative ? -value : value;
}

int atoi(const char *s)
{
    return (int)strtol(s, NULL, 10);
}

int abs(int x)
{
    return x < 0 ? -x : x;
}

/* unistd.h / sys/wait.h helpers that are not system calls themselves */

unsigned int sleep(unsigned int seconds)
{
    sys_sleep((int)(seconds * 100));

    return 0;
}

int msleep(unsigned int ms)
{
    /* Round up to whole 10 ms ticks. */
    return sys_sleep((int)((ms + 9) / 10));
}

pid_t wait(int *status)
{
    return waitpid(-1, status, 0);
}
