#include "include/vfs.h"
#include "include/simplefs.h"
#include "include/memory.h"
#include "include/string.h"
#include "include/wait.h"

/*
 * SimpleFS keeps state between disk requests (a static lookup handle, the
 * file table it is rewriting), and disk requests now sleep. One lock around
 * every operation keeps processes from interleaving inside it.
 */
static kmutex_t vfs_lock = KMUTEX_INIT;

void vfs_init(void)
{
}

int vfs_create(const char *name)
{
    kmutex_lock(&vfs_lock);
    int r = simplefs_create(name);
    kmutex_unlock(&vfs_lock);

    return r;
}

int vfs_write(const char *name, const void *data, size_t size)
{
    kmutex_lock(&vfs_lock);
    int r = simplefs_write(name, data, size);
    kmutex_unlock(&vfs_lock);

    return r;
}

int vfs_read(const char *name, void *buffer, uint32_t buffer_size)
{
    kmutex_lock(&vfs_lock);
    int r = simplefs_read(name, buffer, buffer_size);
    kmutex_unlock(&vfs_lock);

    return r;
}

int vfs_read_at(const char *name, uint32_t offset, void *buffer, uint32_t len)
{
    kmutex_lock(&vfs_lock);
    int r = simplefs_read_at(name, offset, buffer, len);
    kmutex_unlock(&vfs_lock);

    return r;
}

/*
 * SimpleFS can only store a whole file, so the file is read, patched in
 * memory and written back. A gap left by seeking past the end reads as zeros.
 */
int vfs_write_at(const char *name, uint32_t offset, const void *data, uint32_t len)
{
    if (offset + len < offset || offset + len > SIMPLEFS_MAX_FILE_SIZE)
        return -1;

    kmutex_lock(&vfs_lock);

    int result = -1;
    uint8_t *buf = 0;
    simplefs_handle_t *handle = simplefs_find(name);

    if (!handle)
        goto out;

    uint32_t old_size = handle->entry.size;
    uint32_t new_size = offset + len > old_size ? offset + len : old_size;

    buf = kmalloc(new_size);

    if (!buf)
        goto out;

    memset(buf, 0, new_size);

    if (old_size > 0 && simplefs_read(name, buf, new_size) != (int)old_size)
        goto out;

    memcpy(buf + offset, data, len);

    if (simplefs_write(name, buf, new_size) == 0)
        result = (int)len;

out:
    kmutex_unlock(&vfs_lock);
    kfree(buf);

    return result;
}

int vfs_delete(const char *name)
{
    kmutex_lock(&vfs_lock);
    int r = simplefs_delete(name);
    kmutex_unlock(&vfs_lock);

    return r;
}

void vfs_list(void)
{
    kmutex_lock(&vfs_lock);
    simplefs_list();
    kmutex_unlock(&vfs_lock);
}

int vfs_size(const char *name)
{
    kmutex_lock(&vfs_lock);

    simplefs_handle_t *handle = simplefs_find(name);
    int size = handle ? (int)handle->entry.size : -1;

    kmutex_unlock(&vfs_lock);

    return size;
}

int vfs_exists(const char *name)
{
    return vfs_size(name) >= 0;
}
