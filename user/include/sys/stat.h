#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include "abi.h"    /* struct stat, S_IFREG */

int stat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
int mkdir(const char *path);

#define S_ISREG(m) ((m) == S_IFREG)
#define S_ISDIR(m) ((m) == S_IFDIR)
#define S_ISCHR(m) ((m) == S_IFCHR)
#define S_ISFIFO(m) ((m) == S_IFIFO)

#endif
