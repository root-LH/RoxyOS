#ifndef VFS_H
#define VFS_H

#include "types.h"
#include "simplefs.h"

void vfs_init(void);

int vfs_create(const char *name);
int vfs_write(const char *name, const void *data, size_t size);
int vfs_read(const char *name, void *buffer, uint32_t buffer_size);

/* Reads up to len bytes at offset. Returns the count (0 at EOF) or -1. */
int vfs_read_at(const char *name, uint32_t offset, void *buffer, uint32_t len);

/* Writes len bytes at offset, growing the file if needed. Returns len or -1. */
int vfs_write_at(const char *name, uint32_t offset, const void *data, uint32_t len);
int vfs_delete(const char *name);
void vfs_list(void);
int vfs_size(const char *name);
int vfs_exists(const char *name);

#endif