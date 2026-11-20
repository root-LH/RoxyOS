#ifndef DEVFS_H
#define DEVFS_H

#include "vfs.h"

/*
 * /dev: a fixed set of character devices, kept in memory.
 *   tty   keyboard in (a line at a time, echoed), screen + COM1 out
 *   null  reads end at once, writes are discarded
 *   zero  reads return zeros, writes are discarded
 */
fs_t *devfs_create(void);

#endif
