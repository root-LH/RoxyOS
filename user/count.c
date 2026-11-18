#include "ulib.h"

/* Exercises sys_sleep (preemption while blocked) and exit codes. */
int main(void)
{
    for (int i = 1; i <= 3; i++)
    {
        print("count ");
        print_int(i);
        print("\n");
        sys_sleep(20);
    }

    return 3;
}
