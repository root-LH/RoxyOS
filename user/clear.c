#include <stdio.h>

/* clear: blanks the screen and puts the cursor at the top left. */
int main(void)
{
    printf("\033[0m\033[2J\033[H");

    return 0;
}
