#include "../include/devfs.h"
#include "../include/keyboard.h"
#include "../include/console.h"
#include "../include/vga.h"
#include "../include/printk.h"
#include "../include/string.h"

enum
{
    DEV_ROOT = 1,
    DEV_TTY,
    DEV_NULL,
    DEV_ZERO,
    DEV_COUNT
};

static const char *const dev_names[DEV_COUNT] =
{
    [DEV_TTY]  = "tty",
    [DEV_NULL] = "null",
    [DEV_ZERO] = "zero",
};

static fs_t devfs;

/* Not in the inode cache: these live as long as the kernel. */
static inode_t dev_inodes[DEV_COUNT];

/* Cooked console input: echo, backspace editing, returns after '\n'. */
static int tty_read(char *buf, uint32_t len)
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

static int dev_lookup(inode_t *dir, const char *name, inode_t **out)
{
    (void)dir;

    for (int i = DEV_TTY; i < DEV_COUNT; i++)
    {
        if (strcmp(dev_names[i], name) == 0)
        {
            *out = inode_dup(&dev_inodes[i]);
            return 0;
        }
    }

    return -1;
}

static int dev_create(inode_t *dir, const char *name, int type, inode_t **out)
{
    (void)dir; (void)name; (void)type; (void)out;

    return -1;
}

static int dev_remove(inode_t *dir, const char *name, int dir_only)
{
    (void)dir; (void)name; (void)dir_only;

    return -1;
}

static int dev_rename(inode_t *a, const char *an, inode_t *b, const char *bn)
{
    (void)a; (void)an; (void)b; (void)bn;

    return -1;
}

static int dev_readdir(inode_t *dir, uint32_t *pos, struct dirent *out)
{
    (void)dir;

    /* *pos counts entries: ".", "..", then the devices. */
    uint32_t i = *pos;

    if (i >= 2 + DEV_COUNT - DEV_TTY)
        return 0;

    if (i < 2)
    {
        out->d_ino = DEV_ROOT;
        out->d_type = INODE_DIR;
        strcpy(out->d_name, i == 0 ? "." : "..");
    }
    else
    {
        uint32_t dev = DEV_TTY + (i - 2);

        out->d_ino = dev;
        out->d_type = INODE_CHAR;
        strcpy(out->d_name, dev_names[dev]);
    }

    (*pos)++;

    return 1;
}

static int dev_read(inode_t *inode, uint32_t offset, void *buf, uint32_t len)
{
    (void)offset;

    switch (inode->ino)
    {
        case DEV_TTY:  return tty_read(buf, len);
        case DEV_NULL: return 0;
        case DEV_ZERO: memset(buf, 0, len); return (int)len;
        default:       return -1;
    }
}

static int dev_write(inode_t *inode, uint32_t offset, const void *buf, uint32_t len)
{
    (void)offset;

    switch (inode->ino)
    {
        case DEV_TTY:
            printk_write(buf, len);
            return (int)len;

        case DEV_NULL:
        case DEV_ZERO:
            return (int)len;

        default:
            return -1;
    }
}

static int dev_truncate(inode_t *inode)
{
    (void)inode;

    return 0;
}

static int dev_statfs(fs_t *fs, statfs_t *out)
{
    (void)fs;

    memset(out, 0, sizeof(*out));

    return 0;
}

static const fs_ops_t devfs_ops =
{
    .lookup   = dev_lookup,
    .create   = dev_create,
    .remove   = dev_remove,
    .rename   = dev_rename,
    .readdir  = dev_readdir,
    .read     = dev_read,
    .write    = dev_write,
    .truncate = dev_truncate,
    .statfs   = dev_statfs,
};

fs_t *devfs_create(void)
{
    devfs.type = "devfs";
    devfs.ops = &devfs_ops;

    for (int i = DEV_ROOT; i < DEV_COUNT; i++)
    {
        inode_t *inode = &dev_inodes[i];

        inode->fs = &devfs;
        inode->ino = (uint32_t)i;
        inode->refcount = 1;        /* permanent */
        inode->type = i == DEV_ROOT ? INODE_DIR : INODE_CHAR;
        inode->nlink = 1;
    }

    devfs.root = &dev_inodes[DEV_ROOT];

    return &devfs;
}
