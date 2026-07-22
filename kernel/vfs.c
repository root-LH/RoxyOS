#include "include/vfs.h"
#include "include/string.h"
#include "include/memory.h"
#include "include/string.h"

#define MAX_FILES 64

static file_t files[MAX_FILES];

void vfs_init(void)
{
    memset(files, 0, sizeof(files));
}

file_t *vfs_create(const char *name)
{
    if (vfs_find(name) != NULL)
        return NULL;

    for (size_t i = 0; i < MAX_FILES; i++)
    {
        if (!files[i].used)
        {
            files[i].used = 1;

            strcpy(files[i].name, name);

            files[i].data = NULL;
            files[i].size = 0;

            return &files[i];
        }
    }

    return NULL;
}

file_t *vfs_find(const char *name)
{
    for (size_t i = 0; i < MAX_FILES; i++)
    {
        if (!files[i].used)
            continue;

        if (strcmp(files[i].name, name) == 0)
            return &files[i];
    }

    return NULL;
}

int vfs_write(file_t *file, const void *data, size_t size)
{
    if (file == NULL || data == NULL)
        return -1;

    void *buffer = kmalloc(size);

    if (buffer == NULL)
        return -1;

    memcpy(buffer, data, size);

    if (file->data != NULL)
        kfree(file->data);

    file->data = buffer;
    file->size = size;

    return 0;
}

const void *vfs_read(const file_t *file)
{
    if (file == NULL)
        return NULL;

    return file->data;
}

int vfs_delete(file_t *file)
{
    if (file == NULL)
        return -1;

    if (!file->used)
        return -1;

    if (file->data != NULL)
    {
        kfree(file->data);
        file->data = NULL;
    }

    file->size = 0;
    file->name[0] = '\0';
    file->used = 0;

    return 0;
}