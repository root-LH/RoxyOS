#ifndef SYSCALL_H
#define SYSCALL_H

#include "isr.h"

#define SYS_WRITE 0
#define SYS_GETCHAR 1
#define SYS_OPEN 2
#define SYS_READ 3
#define SYS_CLOSE 4
#define SYS_EXIT 5

void syscall_handler(registers_t *r);

/* User Space Wrapper */
void sys_write(const char *str);
char sys_getchar(void);
int sys_open(const char *path);
int sys_read(int fd, void *buffer, uint32_t size);
int sys_close(int fd);
void sys_exit(int status);

#endif