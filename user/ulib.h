#ifndef ULIB_H
#define ULIB_H

typedef unsigned int uint32_t;

/* System call wrappers (user/lib/syscall.asm) */
int  write(int fd, const void *buf, uint32_t len);
int  read(int fd, void *buf, uint32_t len);
int  open(const char *path, int flags);
int  close(int fd);
void _exit(int code) __attribute__((noreturn));
void sys_sleep(int ticks);

/* Tiny helpers (user/ulib.c) */
uint32_t strlen(const char *s);
void print(const char *s);
void print_int(int value);

#endif
