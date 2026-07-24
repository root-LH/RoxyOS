#ifndef VFS_H
#define VFS_H

#include "types.h"
#include "simplefs.h"

void vfs_init(void);

int vfs_create(const char *name);
int vfs_write(const char *name, const void *data, size_t size);
int vfs_read(const char *name, void *buffer, uint32_t buffer_size);
int vfs_delete(const char *name);
void vfs_list(void);

#endif