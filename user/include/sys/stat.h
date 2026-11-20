#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include "abi.h"    /* struct stat, S_IFREG */

int stat(const char *path, struct stat *st);

#endif
