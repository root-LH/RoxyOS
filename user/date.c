#include <stdio.h>
#include <time.h>

/* date: prints the current date and time from the RTC. */
int main(void)
{
    char buf[64];
    time_t now = time(NULL);

    strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Y", gmtime(&now));
    printf("%s\n", buf);

    return 0;
}
