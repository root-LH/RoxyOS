#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include "abi.h"    /* STDIN_FILENO, SEEK_*, ... (kernel/include/uapi) */

typedef int pid_t;
typedef int ssize_t;
typedef int off_t;

/* System calls (user/lib/syscall.asm). -1 means failure. */
ssize_t read(int fd, void *buf, size_t len);
ssize_t write(int fd, const void *buf, size_t len);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);

/* Returns the child's pid in the parent and 0 in the child. */
pid_t fork(void);

/*
 * Replaces this program with path. argv is NULL-terminated (NULL means
 * { path, NULL }). Returns only on failure. Open descriptors stay open.
 */
int exec(const char *path, char *const argv[]);

pid_t getpid(void);

/* Moves the heap break by increment bytes; returns the old break or (void *)-1. */
void *sbrk(int increment);

/* Ends process pid (exit code EXIT_KILLED). */
int kill(pid_t pid);

int yield(void);

/* Milliseconds since boot. */
unsigned int uptime_ms(void);

/* Files and directories. Paths are relative to the working directory. */
int unlink(const char *path);
int rmdir(const char *path);
int rename(const char *old_path, const char *new_path);
int chdir(const char *path);

/* Copies the working directory into buf; returns buf, or NULL if it does not fit. */
char *getcwd(char *buf, size_t size);

/* One raw key from the keyboard, without echo or line editing. */
int getkey(void);

void _exit(int code) __attribute__((noreturn));

/* Timer ticks (100 per second). */
int sys_sleep(int ticks);

/* user/lib/stdlib.c */
unsigned int sleep(unsigned int seconds);
int msleep(unsigned int ms);

#endif
