#ifndef FILE_H
#define FILE_H

#include "types.h"
#include "uapi/abi.h"
#include "vfs.h"

struct process;

/* Open files shared by all processes (fork shares them, like dup). */
#define MAX_OPEN_FILES 64

typedef enum
{
    FILE_NONE = 0,
    FILE_INODE,     /* a file, directory or device found through the VFS */
} file_type_t;

/*
 * An open file. Descriptors in different processes may point at the same
 * file_t (after fork); they then share the offset, as in POSIX.
 */
typedef struct file
{
    file_type_t type;
    int refcount;       /* 0 = free slot */
    int flags;          /* O_* from open() */
    uint32_t offset;    /* bytes; for directories the readdir position */
    inode_t *inode;
} file_t;

void file_init(void);

/* Opens path relative to the caller's working directory. 0 on failure. */
file_t *file_open(const char *path, int flags);

file_t *file_dup(file_t *file);

/* Drops one reference; the file is freed with the last one. Never sleeps. */
void file_close(file_t *file);

/*
 * Kernel-buffer I/O at the file offset, which advances. read returns 0 at
 * end of file; /dev/tty reads return one line (or len bytes), echoing it.
 * Both return -1 on error (or when the reader is killed while waiting).
 */
int file_read(file_t *file, void *buf, uint32_t len);
int file_write(file_t *file, const void *buf, uint32_t len);

/* Returns the new offset, or -1 (devices cannot seek). */
int file_seek(file_t *file, int32_t offset, int whence);

/* Next directory entry: 1, or 0 at the end, -1 if file is not a directory. */
int file_readdir(file_t *file, struct dirent *out);

int file_stat(file_t *file, struct stat *st);

/*
 * Per-process descriptor table (process_t::files). Kernel threads have
 * one too, but normally leave it empty.
 */
int fd_install(struct process *proc, file_t *file);  /* lowest free fd, or -1 */
file_t *fd_get(struct process *proc, int fd);
int fd_close(struct process *proc, int fd);
void fd_close_all(struct process *proc);

/* Gives dst the same open files as src (each one gains a reference). */
void fd_copy(struct process *dst, struct process *src);

/* stdin, stdout and stderr on /dev/tty. Returns -1 if it cannot be opened. */
int fd_open_stdio(struct process *proc);

#endif
