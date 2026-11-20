#include "include/file.h"
#include "include/process.h"
#include "include/vfs.h"
#include "include/string.h"
#include "include/cpu.h"

static file_t file_table[MAX_OPEN_FILES];

void file_init(void)
{
    memset(file_table, 0, sizeof(file_table));
}

static file_t *file_alloc(file_type_t type, int flags)
{
    uint32_t irq = irq_save();

    for (int i = 0; i < MAX_OPEN_FILES; i++)
    {
        file_t *file = &file_table[i];

        if (file->refcount != 0)
            continue;

        memset(file, 0, sizeof(file_t));
        file->type = type;
        file->flags = flags;
        file->refcount = 1;

        irq_restore(irq);
        return file;
    }

    irq_restore(irq);
    return 0;
}

file_t *file_open(const char *path, int flags)
{
    inode_t *inode;

    if (vfs_open(path, flags, &inode) != 0)
        return 0;

    file_t *file = file_alloc(FILE_INODE, flags);

    if (!file)
    {
        inode_put(inode);
        return 0;
    }

    file->inode = inode;

    return file;
}

int file_pipe(file_t **read_end, file_t **write_end)
{
    pipe_t *pipe = pipe_create();

    if (!pipe)
        return -1;

    file_t *r = file_alloc(FILE_PIPE, O_RDONLY);
    file_t *w = file_alloc(FILE_PIPE, O_WRONLY);

    if (!r || !w)
    {
        if (r)
            r->refcount = 0;

        if (w)
            w->refcount = 0;

        pipe_close(pipe, 0);
        pipe_close(pipe, 1);
        return -1;
    }

    r->pipe = w->pipe = pipe;
    *read_end = r;
    *write_end = w;

    return 0;
}

file_t *file_dup(file_t *file)
{
    uint32_t irq = irq_save();

    file->refcount++;

    irq_restore(irq);

    return file;
}

void file_close(file_t *file)
{
    uint32_t irq = irq_save();
    inode_t *inode = 0;
    pipe_t *pipe = 0;
    int write_end = (file->flags & O_ACCMODE) != O_RDONLY;

    if (file->refcount > 0 && --file->refcount == 0)
    {
        inode = file->inode;
        pipe = file->pipe;
        file->inode = 0;
        file->pipe = 0;
        file->type = FILE_NONE;
    }

    irq_restore(irq);

    inode_put(inode);

    if (pipe)
        pipe_close(pipe, write_end);
}

static int readable(file_t *file)
{
    return (file->flags & O_ACCMODE) != O_WRONLY;
}

static int writable(file_t *file)
{
    return (file->flags & O_ACCMODE) != O_RDONLY;
}

int file_read(file_t *file, void *buf, uint32_t len)
{
    if (!readable(file))
        return -1;

    if (len == 0)
        return 0;

    if (file->type == FILE_PIPE)
        return pipe_read(file->pipe, buf, len);

    inode_t *inode = file->inode;

    if (inode->type == INODE_DIR)
        return -1;

    int n = inode->fs->ops->read(inode, file->offset, buf, len);

    if (n > 0 && inode->type == INODE_FILE)
        file->offset += n;

    return n;
}

int file_write(file_t *file, const void *buf, uint32_t len)
{
    if (!writable(file))
        return -1;

    if (len == 0)
        return 0;

    if (file->type == FILE_PIPE)
        return pipe_write(file->pipe, buf, len);

    inode_t *inode = file->inode;

    if (inode->type == INODE_FILE && (file->flags & O_APPEND))
        file->offset = inode->size;

    int n = inode->fs->ops->write(inode, file->offset, buf, len);

    if (n > 0 && inode->type == INODE_FILE)
        file->offset += n;

    return n;
}

int file_seek(file_t *file, int32_t offset, int whence)
{
    inode_t *inode = file->inode;

    if (file->type != FILE_INODE ||
        (inode->type != INODE_FILE && inode->type != INODE_DIR))
        return -1;

    int32_t base;

    switch (whence)
    {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = (int32_t)file->offset; break;
        case SEEK_END: base = (int32_t)inode->size; break;
        default:       return -1;
    }

    int32_t pos = base + offset;

    /* Seeking past the end is allowed; the gap reads as zeros. */
    if (pos < 0 || (offset > 0 && pos < base))
        return -1;

    file->offset = (uint32_t)pos;

    return pos;
}

int file_readdir(file_t *file, struct dirent *out)
{
    inode_t *inode = file->inode;

    if (file->type != FILE_INODE || inode->type != INODE_DIR)
        return -1;

    return inode->fs->ops->readdir(inode, &file->offset, out);
}

int file_stat(file_t *file, struct stat *st)
{
    if (file->type == FILE_PIPE)
    {
        memset(st, 0, sizeof(*st));
        st->st_mode = S_IFIFO;
        st->st_size = file->pipe->count;    /* bytes waiting to be read */
        st->st_nlink = 1;
        return 0;
    }

    vfs_inode_stat(file->inode, st);

    return 0;
}

int fd_install(process_t *proc, file_t *file)
{
    for (int fd = 0; fd < PROCESS_MAX_FD; fd++)
    {
        if (!proc->files[fd])
        {
            proc->files[fd] = file;
            return fd;
        }
    }

    return -1;
}

file_t *fd_get(process_t *proc, int fd)
{
    if (!proc || fd < 0 || fd >= PROCESS_MAX_FD)
        return 0;

    return proc->files[fd];
}

int fd_close(process_t *proc, int fd)
{
    file_t *file = fd_get(proc, fd);

    if (!file)
        return -1;

    proc->files[fd] = 0;
    file_close(file);

    return 0;
}

void fd_close_all(process_t *proc)
{
    for (int fd = 0; fd < PROCESS_MAX_FD; fd++)
        fd_close(proc, fd);
}

void fd_copy(process_t *dst, process_t *src)
{
    for (int fd = 0; fd < PROCESS_MAX_FD; fd++)
        dst->files[fd] = src->files[fd] ? file_dup(src->files[fd]) : 0;
}

int fd_open_stdio(process_t *proc)
{
    file_t *tty = file_open("/dev/tty", O_RDWR);

    if (!tty)
        return -1;

    /* One open file behind 0, 1 and 2, like a terminal. */
    proc->files[STDIN_FILENO]  = tty;
    proc->files[STDOUT_FILENO] = file_dup(tty);
    proc->files[STDERR_FILENO] = file_dup(tty);

    return 0;
}
