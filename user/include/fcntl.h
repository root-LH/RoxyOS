#ifndef _FCNTL_H
#define _FCNTL_H

#include "abi.h"    /* O_RDONLY, O_CREAT, ... */

/* Returns a file descriptor, or -1. O_CREAT creates a missing file. */
int open(const char *path, int flags);

#endif
