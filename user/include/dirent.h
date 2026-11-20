#ifndef _DIRENT_H
#define _DIRENT_H

#include "abi.h"    /* struct dirent */

/* A directory open for reading (user/lib/dirent.c). */
typedef struct DIR
{
    int fd;
    struct dirent entry;
} DIR;

DIR *opendir(const char *path);

/* Next entry ("." and ".." included), or NULL at the end. */
struct dirent *readdir(DIR *dir);

int closedir(DIR *dir);

/* The raw system call: 1 with *out filled, 0 at the end, -1 on error. */
int getdirent(int fd, struct dirent *out);

#endif
