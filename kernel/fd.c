#include "include/fd.h"
#include "include/string.h"

static fd_entry_t fd_table[MAX_FD];

void fd_init(void)
{
    memset(fd_table, 0, sizeof(fd_table));
}

int fd_alloc(const char *filename)
{
    for (int i = 0; i < MAX_FD; i++)
    {
        if (!fd_table[i].used)
        {
            fd_table[i].used = 1;

            strcpy(fd_table[i].filename, filename);

            return i;
        }
    }

    return -1;
}

void fd_close(int fd)
{
    if (fd < 0 || fd >= MAX_FD)
        return;

    fd_table[fd].used = 0;
}

const char *fd_filename(int fd)
{
    if (fd < 0 || fd >= MAX_FD)
        return 0;

    if (!fd_table[fd].used)
        return 0;

    return fd_table[fd].filename;
}

int fd_valid(int fd)
{
    if (fd < 0 || fd >= MAX_FD)
        return 0;

    return fd_table[fd].used;
}