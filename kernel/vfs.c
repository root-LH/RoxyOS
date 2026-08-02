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

int vfs_size(const char *name)
{
    simplefs_handle_t *handle = simplefs_find(name);

    if (handle == 0)
        return -1;

    return handle->entry.size;
}

int vfs_exists(const char *name)
{
    return simplefs_find(name) != 0;
}