#include "../include/vfs.h"
#include "../include/rxfs.h"
#include "../include/devfs.h"
#include "../include/ata.h"
#include "../include/process.h"
#include "../include/memory.h"
#include "../include/string.h"
#include "../include/printk.h"
#include "../include/panic.h"
#include "../include/cpu.h"

/* Sectors before the file system: boot code and kernel (boot/stage1.asm). */
#define FS_START_SECTOR 2048

/* ------------------------------------------------------------------ */
/* Inode cache                                                         */
/* ------------------------------------------------------------------ */

static inode_t inode_cache[INODE_CACHE_SIZE];

inode_t *inode_get(fs_t *fs, uint32_t ino, int *is_new)
{
    uint32_t flags = irq_save();
    inode_t *free_slot = 0;

    *is_new = 0;

    for (int i = 0; i < INODE_CACHE_SIZE; i++)
    {
        inode_t *inode = &inode_cache[i];

        if (inode->refcount == 0)
        {
            if (!free_slot)
                free_slot = inode;
            continue;
        }

        if (inode->fs == fs && inode->ino == ino)
        {
            inode->refcount++;
            irq_restore(flags);
            return inode;
        }
    }

    if (free_slot)
    {
        memset(free_slot, 0, sizeof(inode_t));
        free_slot->fs = fs;
        free_slot->ino = ino;
        free_slot->refcount = 1;
        *is_new = 1;
    }
    else
    {
        klog("[vfs] inode cache full\n");
    }

    irq_restore(flags);

    return free_slot;
}

inode_t *inode_find(fs_t *fs, uint32_t ino)
{
    for (int i = 0; i < INODE_CACHE_SIZE; i++)
    {
        inode_t *inode = &inode_cache[i];

        if (inode->refcount > 0 && inode->fs == fs && inode->ino == ino)
            return inode;
    }

    return 0;
}

inode_t *inode_dup(inode_t *inode)
{
    uint32_t flags = irq_save();
    inode->refcount++;
    irq_restore(flags);

    return inode;
}

void inode_put(inode_t *inode)
{
    if (!inode)
        return;

    uint32_t flags = irq_save();

    /* Metadata is written through on every change, so nothing to flush. */
    if (inode->refcount > 0)
        inode->refcount--;

    irq_restore(flags);
}

/* ------------------------------------------------------------------ */
/* Mount table                                                         */
/* ------------------------------------------------------------------ */

typedef struct
{
    char path[PATH_MAX];
    fs_t *fs;
} mount_t;

static mount_t mounts[MAX_MOUNTS];
static int mount_count = 0;

int vfs_mount(const char *path, fs_t *fs)
{
    if (mount_count == MAX_MOUNTS || strlen(path) >= PATH_MAX)
        return -1;

    strcpy(mounts[mount_count].path, path);
    mounts[mount_count].fs = fs;
    mount_count++;

    klog("[vfs] %s mounted on %s\n", fs->type, path);

    return 0;
}

static int is_mount_point(const char *abs)
{
    for (int i = 0; i < mount_count; i++)
    {
        if (strcmp(mounts[i].path, abs) == 0)
            return 1;
    }

    return 0;
}

/*
 * The mount whose path is the longest prefix of abs (on a component
 * boundary). *rest points at what is left to look up inside it.
 */
static mount_t *find_mount(const char *abs, const char **rest)
{
    mount_t *best = 0;
    uint32_t best_len = 0;

    for (int i = 0; i < mount_count; i++)
    {
        const char *mp = mounts[i].path;
        uint32_t len = strlen(mp);

        if (strcmp(mp, "/") == 0)
            len = 0;        /* matches everything */
        else if (memcmp(abs, mp, len) != 0 || (abs[len] != '/' && abs[len] != '\0'))
            continue;

        if (!best || len > best_len)
        {
            best = &mounts[i];
            best_len = len;
        }
    }

    if (best)
        *rest = abs + best_len;

    return best;
}

/* ------------------------------------------------------------------ */
/* Paths                                                               */
/* ------------------------------------------------------------------ */

const char *vfs_getcwd(void)
{
    process_t *proc = process_current();

    return (proc && proc->cwd[0]) ? proc->cwd : "/";
}

int vfs_abspath(const char *cwd, const char *path, char *out)
{
    /* Component start offsets in out, to pop on "..". */
    uint32_t starts[PATH_MAX / 2];
    int depth = 0;
    uint32_t len = 0;

    out[0] = '\0';

    for (int pass = 0; pass < 2; pass++)
    {
        const char *p = pass == 0 ? cwd : path;

        if (pass == 0 && path[0] == '/')
            continue;       /* absolute: ignore cwd */

        while (*p)
        {
            while (*p == '/')
                p++;

            if (*p == '\0')
                break;

            const char *name = p;
            uint32_t n = 0;

            while (p[n] && p[n] != '/')
                n++;

            p += n;

            if (n == 1 && name[0] == '.')
                continue;

            if (n == 2 && name[0] == '.' && name[1] == '.')
            {
                if (depth > 0)
                    len = starts[--depth];

                out[len] = '\0';
                continue;
            }

            if (n > NAME_MAX || len + 1 + n >= PATH_MAX)
                return -1;

            starts[depth++] = len;
            out[len++] = '/';
            memcpy(out + len, name, n);
            len += n;
            out[len] = '\0';
        }
    }

    if (len == 0)
        strcpy(out, "/");

    return 0;
}

static int make_abs(const char *path, char *out)
{
    if (!path || path[0] == '\0')
        return -1;

    return vfs_abspath(vfs_getcwd(), path, out);
}

/* Looks up a normalized absolute path. */
static int walk(const char *abs, inode_t **out)
{
    const char *rest;
    mount_t *m = find_mount(abs, &rest);

    if (!m)
        return -1;

    inode_t *cur = inode_dup(m->fs->root);
    char name[NAME_MAX + 1];

    while (*rest)
    {
        while (*rest == '/')
            rest++;

        if (*rest == '\0')
            break;

        uint32_t n = 0;

        while (rest[n] && rest[n] != '/')
        {
            name[n] = rest[n];
            n++;
        }

        name[n] = '\0';
        rest += n;

        inode_t *next;

        if (cur->type != INODE_DIR || cur->fs->ops->lookup(cur, name, &next) != 0)
        {
            inode_put(cur);
            return -1;
        }

        inode_put(cur);
        cur = next;
    }

    *out = cur;

    return 0;
}

/*
 * Splits path into its directory (looked up, referenced) and last name.
 * Mount points themselves cannot be created, removed or renamed.
 */
static int walk_parent(const char *path, inode_t **dir, char *name)
{
    char abs[PATH_MAX];

    if (make_abs(path, abs) != 0 || strcmp(abs, "/") == 0 || is_mount_point(abs))
        return -1;

    char *slash = abs;

    for (char *p = abs; *p; p++)
    {
        if (*p == '/')
            slash = p;
    }

    strcpy(name, slash + 1);

    if (slash == abs)
        slash[1] = '\0';    /* parent is "/" */
    else
        *slash = '\0';

    if (walk(abs, dir) != 0)
        return -1;

    if ((*dir)->type != INODE_DIR)
    {
        inode_put(*dir);
        return -1;
    }

    return 0;
}

int vfs_lookup(const char *path, inode_t **out)
{
    char abs[PATH_MAX];

    if (make_abs(path, abs) != 0)
        return -1;

    return walk(abs, out);
}

int vfs_open(const char *path, int flags, inode_t **out)
{
    int writing = (flags & O_ACCMODE) != O_RDONLY;
    inode_t *inode;

    if (vfs_lookup(path, &inode) != 0)
    {
        if (!(flags & O_CREAT))
            return -1;

        inode_t *dir;
        char name[NAME_MAX + 1];

        if (walk_parent(path, &dir, name) != 0)
            return -1;

        int r = dir->fs->ops->create(dir, name, INODE_FILE, &inode);

        /* Someone else may have created it in between. */
        if (r != 0)
            r = dir->fs->ops->lookup(dir, name, &inode);

        inode_put(dir);

        if (r != 0)
            return -1;
    }
    else if (inode->type == INODE_DIR && writing)
    {
        inode_put(inode);
        return -1;
    }
    else if ((flags & O_TRUNC) && writing && inode->type == INODE_FILE)
    {
        if (inode->fs->ops->truncate(inode) != 0)
        {
            inode_put(inode);
            return -1;
        }
    }

    *out = inode;

    return 0;
}

int vfs_mkdir(const char *path)
{
    inode_t *dir, *child;
    char name[NAME_MAX + 1];

    if (walk_parent(path, &dir, name) != 0)
        return -1;

    int r = dir->fs->ops->create(dir, name, INODE_DIR, &child);

    inode_put(dir);

    if (r == 0)
        inode_put(child);

    return r;
}

static int vfs_remove(const char *path, int dir_only)
{
    inode_t *dir;
    char name[NAME_MAX + 1];

    if (walk_parent(path, &dir, name) != 0)
        return -1;

    int r = dir->fs->ops->remove(dir, name, dir_only);

    inode_put(dir);

    return r;
}

int vfs_unlink(const char *path)
{
    return vfs_remove(path, 0);
}

int vfs_rmdir(const char *path)
{
    return vfs_remove(path, 1);
}

int vfs_rename(const char *old_path, const char *new_path)
{
    char old_abs[PATH_MAX], new_abs[PATH_MAX];

    if (make_abs(old_path, old_abs) != 0 || make_abs(new_path, new_abs) != 0)
        return -1;

    /* A directory cannot move inside itself. */
    uint32_t len = strlen(old_abs);

    if (memcmp(new_abs, old_abs, len) == 0 && new_abs[len] == '/')
        return -1;

    inode_t *old_dir, *new_dir;
    char old_name[NAME_MAX + 1], new_name[NAME_MAX + 1];

    if (walk_parent(old_abs, &old_dir, old_name) != 0)
        return -1;

    if (walk_parent(new_abs, &new_dir, new_name) != 0)
    {
        inode_put(old_dir);
        return -1;
    }

    int r = -1;

    if (old_dir->fs == new_dir->fs)
        r = old_dir->fs->ops->rename(old_dir, old_name, new_dir, new_name);

    inode_put(old_dir);
    inode_put(new_dir);

    return r;
}

void vfs_inode_stat(inode_t *inode, struct stat *st)
{
    st->st_size = inode->size;
    st->st_mode = (unsigned int)inode->type;
    st->st_ino = inode->ino;
    st->st_nlink = inode->nlink;
}

int vfs_stat(const char *path, struct stat *st)
{
    inode_t *inode;

    if (vfs_lookup(path, &inode) != 0)
        return -1;

    vfs_inode_stat(inode, st);
    inode_put(inode);

    return 0;
}

int vfs_chdir(const char *path)
{
    process_t *proc = process_current();
    char abs[PATH_MAX];
    inode_t *inode;

    if (!proc || make_abs(path, abs) != 0 || walk(abs, &inode) != 0)
        return -1;

    int is_dir = inode->type == INODE_DIR;

    inode_put(inode);

    if (!is_dir)
        return -1;

    strcpy(proc->cwd, abs);

    return 0;
}

int vfs_read_all(const char *path, uint8_t **data, uint32_t *size)
{
    inode_t *inode;

    if (vfs_lookup(path, &inode) != 0)
        return -1;

    int result = -1;
    uint8_t *buf = 0;

    if (inode->type != INODE_FILE)
        goto out;

    /* +1 so an empty file still gets a valid buffer */
    buf = kmalloc(inode->size + 1);

    if (!buf)
        goto out;

    if (inode->size > 0 &&
        inode->fs->ops->read(inode, 0, buf, inode->size) != (int)inode->size)
        goto out;

    *data = buf;
    *size = inode->size;
    buf = 0;
    result = 0;

out:
    kfree(buf);
    inode_put(inode);

    return result;
}

int vfs_statfs(const char *path, statfs_t *out)
{
    inode_t *inode;

    if (vfs_lookup(path, &inode) != 0)
        return -1;

    int r = inode->fs->ops->statfs(inode->fs, out);

    inode_put(inode);

    return r;
}

/* ------------------------------------------------------------------ */
/* Boot                                                                */
/* ------------------------------------------------------------------ */

static fs_t *root_fs;

static uint32_t fs_sectors(void)
{
    uint32_t total = ata_sector_count();

    return total > FS_START_SECTOR ? total - FS_START_SECTOR : 0;
}

/*
 * Standard directories of a fresh file system. Created directly in the
 * root file system: /dev may already be a mount point, which vfs_mkdir
 * refuses to touch.
 */
static void make_skeleton(void)
{
    static const char *const dirs[] = { "bin", "dev", "tmp" };

    for (uint32_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
    {
        inode_t *dir;

        if (root_fs->ops->create(root_fs->root, dirs[i], INODE_DIR, &dir) == 0)
            inode_put(dir);
    }
}

void vfs_init(void)
{
    uint32_t sectors = fs_sectors();

    root_fs = rxfs_mount(FS_START_SECTOR, sectors);

    if (!root_fs)
    {
        kprintf("[vfs] no valid file system on the disk, formatting %u KB\n",
                sectors / 2);

        if (rxfs_format(FS_START_SECTOR, sectors) != 0 ||
            !(root_fs = rxfs_mount(FS_START_SECTOR, sectors)))
            panic("vfs: cannot create a file system on the disk");
    }

    vfs_mount("/", root_fs);

    make_skeleton();

    vfs_mount("/dev", devfs_create());
}

int vfs_format(void)
{
    /* Open files would point at inodes that no longer exist. */
    for (int i = 0; i < INODE_CACHE_SIZE; i++)
    {
        inode_t *inode = &inode_cache[i];

        if (inode->refcount > 0 && inode->fs == root_fs && inode != root_fs->root)
            return -1;
    }

    if (rxfs_reformat(root_fs) != 0)
        return -1;

    make_skeleton();

    return 0;
}
