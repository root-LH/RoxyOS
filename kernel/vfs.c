#include "include/vfs.h"
#include "include/simplefs.h"

void vfs_init(void)
{
    
}

int vfs_create(const char *name)
{
    return simplefs_create(name);
}

int vfs_write(const char *name, const void *data, size_t size)
{
    return simplefs_write(name, data, size);
}

int vfs_read(const char *name, void *buffer, uint32_t buffer_size)
{
    return simplefs_read(name, buffer, buffer_size);
}

int vfs_delete(const char *name)
{
    return simplefs_delete(name);
}

void vfs_list(void)
{
    simplefs_list();
}