#ifndef FILE_H
#define FILE_H

#include "types.h"
#include "uapi/abi.h"

struct process;

/* Open files shared by all processes (fork shares them, like dup). */
#define MAX_OPEN_FILES 64

typedef enum
{
    FILE_NONE = 0,
    FILE_CONSOLE,   /* keyboard in, screen + serial out */
    FILE_REGULAR    /* SimpleFS file */
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
    uint32_t offset;
    char name[PATH_MAX];
} file_t;

void file_init(void);

/* Opens path (O_CREAT / O_TRUNC / O_APPEND honoured). Returns 0 on failure. */
file_t *file_open(const char *path, int flags);

/* A new reference to the console. */
file_t *file_open_console(void);

file_t *file_dup(file_t *file);

/* Drops one reference; the file is freed with the last one. */
void file_close(file_t *file);

/*
 * Kernel-buffer I/O at the file offset, which advances. read returns 0 at
 * end of file; console reads return one line (or len bytes), echoing it.
 * Both return -1 on error (or when the reader is killed while waiting).
 */
int file_read(file_t *file, void *buf, uint32_t len);
int file_write(file_t *file, const void *buf, uint32_t len);

/* Returns the new offset, or -1 (console files cannot seek). */
int file_seek(file_t *file, int32_t offset, int whence);

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

/* stdin, stdout and stderr on the console. Returns -1 if out of files. */
int fd_open_stdio(struct process *proc);

#endif
