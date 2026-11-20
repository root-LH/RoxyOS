#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <unistd.h>

/*
 * Waits for child pid (-1 = any) to exit and stores its exit code in
 * *status. Note: the status is the plain exit code (no W* macros), and
 * EXIT_KILLED for a process ended by kill().
 */
pid_t waitpid(pid_t pid, int *status, int options);

/* waitpid(-1, status, 0) */
pid_t wait(int *status);

#endif
