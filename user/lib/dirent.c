#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

int sys_getcwd(char *buf, size_t size);

DIR *opendir(const char *path)
{
    int fd = open(path, O_RDONLY);

    if (fd < 0)
        return NULL;

    DIR *dir = malloc(sizeof(DIR));

    if (!dir)
    {
        close(fd);
        return NULL;
    }

    dir->fd = fd;

    return dir;
}

struct dirent *readdir(DIR *dir)
{
    return getdirent(dir->fd, &dir->entry) == 1 ? &dir->entry : NULL;
}

int closedir(DIR *dir)
{
    int r = close(dir->fd);

    free(dir);

    return r;
}

char *getcwd(char *buf, size_t size)
{
    return sys_getcwd(buf, size) < 0 ? NULL : buf;
}
