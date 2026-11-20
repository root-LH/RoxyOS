#include "include/file.h"
#include "include/process.h"
#include "include/vfs.h"
#include "include/keyboard.h"
#include "include/console.h"
#include "include/vga.h"
#include "include/printk.h"
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
    if (strlen(path) >= PATH_MAX || path[0] == '\0')
        return 0;

    if (!vfs_exists(path))
    {
        if (!(flags & O_CREAT) || vfs_create(path) != 0)
            return 0;
    }
    else if ((flags & O_TRUNC) && (flags & O_ACCMODE) != O_RDONLY)
    {
        if (vfs_write(path, "", 0) != 0)
            return 0;
    }

    file_t *file = file_alloc(FILE_REGULAR, flags);

    if (!file)
        return 0;

    strcpy(file->name, path);

    return file;
}

file_t *file_open_console(void)
{
    return file_alloc(FILE_CONSOLE, O_RDWR);
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

    if (file->refcount > 0 && --file->refcount == 0)
        file->type = FILE_NONE;

    irq_restore(irq);
}

/* Cooked console input: echo, backspace editing, returns after '\n'. */
static int console_read(char *buf, uint32_t len)
{
    uint32_t n = 0;

    while (n < len)
    {
        int c = keyboard_read(1);

        if (c < 0)
            return n ? (int)n : -1;     /* killed while waiting */

        if (c == '\b')
        {
            if (n > 0)
            {
                n--;
                vga_backspace();
            }
            continue;
        }

        /* Arrow keys have no meaning in a plain line. */
        if (c >= 0x80)
            continue;

        buf[n++] = (char)c;
        console_putchar((char)c);

        if (c == '\n')
            break;
    }

    return (int)n;
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

    if (file->type == FILE_CONSOLE)
        return console_read(buf, len);

    int n = vfs_read_at(file->name, file->offset, buf, len);

    if (n > 0)
        file->offset += n;

    return n;
}

int file_write(file_t *file, const void *buf, uint32_t len)
{
    if (!writable(file))
        return -1;

    if (len == 0)
        return 0;

    if (file->type == FILE_CONSOLE)
    {
        printk_write(buf, len);
        return (int)len;
    }

    if (file->flags & O_APPEND)
    {
        int size = vfs_size(file->name);

        if (size < 0)
            return -1;

        file->offset = (uint32_t)size;
    }

    int n = vfs_write_at(file->name, file->offset, buf, len);

    if (n > 0)
        file->offset += n;

    return n;
}

int file_seek(file_t *file, int32_t offset, int whence)
{
    if (file->type != FILE_REGULAR)
        return -1;

    int32_t base;

    switch (whence)
    {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = (int32_t)file->offset; break;
        case SEEK_END:
            base = vfs_size(file->name);

            if (base < 0)
                return -1;
            break;
        default:
            return -1;
    }

    int32_t pos = base + offset;

    /* Seeking past the end is allowed; a later write fills the gap with zeros. */
    if (pos < 0 || (offset > 0 && pos < base))
        return -1;

    file->offset = (uint32_t)pos;

    return pos;
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
    file_t *console = file_open_console();

    if (!console)
        return -1;

    /* One open file behind 0, 1 and 2, like a terminal. */
    proc->files[STDIN_FILENO]  = console;
    proc->files[STDOUT_FILENO] = file_dup(console);
    proc->files[STDERR_FILENO] = file_dup(console);

    return 0;
}
