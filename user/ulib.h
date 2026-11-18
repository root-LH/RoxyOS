#ifndef ULIB_H
#define ULIB_H

typedef unsigned int uint32_t;

/* System call wrappers (kernel/lib/user/syscall.asm) */
int  sys_write(const char *str);
char sys_getchar(void);
int  sys_open(const char *path);
int  sys_read(int fd, void *buffer, uint32_t size);
int  sys_close(int fd);
void sys_exit(int code) __attribute__((noreturn));
void sys_sleep(int ticks);

/* Tiny helpers (user/ulib.c) */
uint32_t strlen(const char *s);
void print(const char *s);
void print_int(int value);

#endif
