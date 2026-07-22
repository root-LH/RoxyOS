#ifndef VFS_H
#define VFS_H

#include "types.h"

#define MAX_FILE_NAME 32

typedef struct file
{
    char name[MAX_FILE_NAME];

    void *data;
    size_t size;

    uint8_t used;
} file_t;

void vfs_init(void);

file_t *vfs_create(const char *name);
file_t *vfs_find(const char *name);
int vfs_write(file_t *file, const void *data, size_t size);
const void *vfs_read(const file_t *file);
int vfs_delete(file_t *file);

#endif