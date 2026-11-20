#ifndef VFS_H
#define VFS_H

#include "types.h"
#include "uapi/abi.h"

/*
 * Virtual file system.
 *
 *  - fs_t        one mounted file system (rxfs on the disk, devfs, ...),
 *                with an operations table (fs_ops_t) the VFS calls into
 *  - inode_t     an in-memory file or directory. Inodes live in a shared
 *                cache with reference counts, so every open of the same
 *                file sees the same size and blocks
 *  - mount table path -> fs. Paths are made absolute and normalized
 *                ("." and ".." folded) first, then the longest matching
 *                mount point picks the file system and the rest is looked
 *                up component by component from its root
 *
 * Every file system does its own locking; the VFS core itself never holds
 * a lock while calling an operation, so a device read can block freely.
 * Paths are relative to the calling process's working directory.
 */

#define INODE_FILE  S_IFREG
#define INODE_CHAR  S_IFCHR
#define INODE_DIR   S_IFDIR

struct fs;

typedef struct inode
{
    struct fs *fs;
    uint32_t ino;
    int refcount;           /* 0 = free cache slot */

    int type;               /* INODE_* */
    uint16_t nlink;
    uint32_t size;

    /* File-system specific: rxfs block pointers, devfs device number. */
    uint32_t data[14];
} inode_t;

typedef struct
{
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t block_size;
    uint32_t total_inodes;
    uint32_t free_inodes;
} statfs_t;

typedef struct fs_ops
{
    /* Directory operations. The new/found inode comes back referenced. */
    int (*lookup)(inode_t *dir, const char *name, inode_t **out);
    int (*create)(inode_t *dir, const char *name, int type, inode_t **out);
    /* Removes name; dir_only selects rmdir (empty directories) vs unlink (files). */
    int (*remove)(inode_t *dir, const char *name, int dir_only);
    int (*rename)(inode_t *old_dir, const char *old_name,
                  inode_t *new_dir, const char *new_name);
    /*
     * Next entry at or after *pos: fills out, advances *pos and returns 1,
     * or returns 0 at the end (-1 on error).
     */
    int (*readdir)(inode_t *dir, uint32_t *pos, struct dirent *out);

    /* Data. read returns 0 at the end of the file. */
    int (*read)(inode_t *inode, uint32_t offset, void *buf, uint32_t len);
    int (*write)(inode_t *inode, uint32_t offset, const void *buf, uint32_t len);
    /* Drops every data block (O_TRUNC). */
    int (*truncate)(inode_t *inode);

    int (*statfs)(struct fs *fs, statfs_t *out);
} fs_ops_t;

typedef struct fs
{
    const char *type;       /* "rxfs", "devfs" */
    const fs_ops_t *ops;
    inode_t *root;
    void *priv;
} fs_t;

/* ---- Inode cache (fs implementations) ---------------------------- */

#define INODE_CACHE_SIZE 64

/*
 * Returns the cached inode (fs, ino) with one more reference, or a fresh
 * slot (refcount 1, *is_new = 1) for the caller to fill in. 0 if full.
 */
inode_t *inode_get(fs_t *fs, uint32_t ino, int *is_new);

/* Returns the cached inode without taking a reference (0 if not cached). */
inode_t *inode_find(fs_t *fs, uint32_t ino);

inode_t *inode_dup(inode_t *inode);

/*
 * Drops a reference. Never does I/O, so it is safe anywhere (file
 * descriptors are closed from the fault path too).
 */
void inode_put(inode_t *inode);

/* ---- Mounting ----------------------------------------------------- */

#define MAX_MOUNTS 4

/* Mounts fs at an absolute, normalized path ("/" first). Returns 0 or -1. */
int vfs_mount(const char *path, fs_t *fs);

/* Mounts the disk file system on / (formatting it if it is invalid) and devfs on /dev. */
void vfs_init(void);

/* Re-creates an empty file system on the disk (shell mkfs). */
int vfs_format(void);

/* Usage of the file system holding path. */
int vfs_statfs(const char *path, statfs_t *out);

/* ---- Paths -------------------------------------------------------- */

/*
 * Turns path (absolute, or relative to cwd) into a normalized absolute
 * path in out[PATH_MAX]. Returns 0, or -1 if it is too long or a
 * component is longer than NAME_MAX.
 */
int vfs_abspath(const char *cwd, const char *path, char *out);

/* Finds path. *out is referenced (inode_put it). Returns 0 or -1. */
int vfs_lookup(const char *path, inode_t **out);

/*
 * Opens path: O_CREAT creates a missing file, O_TRUNC empties a regular
 * file opened for writing. Directories may only be opened read-only.
 */
int vfs_open(const char *path, int flags, inode_t **out);

int vfs_mkdir(const char *path);
int vfs_unlink(const char *path);
int vfs_rmdir(const char *path);
int vfs_rename(const char *old_path, const char *new_path);
int vfs_stat(const char *path, struct stat *st);
void vfs_inode_stat(inode_t *inode, struct stat *st);

/* Working directory of the calling process. */
int vfs_chdir(const char *path);
const char *vfs_getcwd(void);

/* Reads a whole file into a kmalloc'ed buffer (*size bytes). Returns 0 or -1. */
int vfs_read_all(const char *path, uint8_t **data, uint32_t *size);

#endif
