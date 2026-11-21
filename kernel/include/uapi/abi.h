#ifndef ROXY_ABI_H
#define ROXY_ABI_H

/*
 * Kernel <-> user ABI, shared by the kernel and user/ (libc).
 * Plain C types only: this header is compiled on both sides.
 *
 * System calls: int 0x80, eax = number, args in ebx, ecx, edx.
 * The result comes back in eax; -1 means failure.
 */

#define SYS_WRITE     0   /* write(fd, buf, len) -> bytes written           */
#define SYS_GETCHAR   1   /* getchar() -> raw key, no echo                  */
#define SYS_OPEN      2   /* open(path, flags) -> fd                        */
#define SYS_READ      3   /* read(fd, buf, len) -> bytes read, 0 at EOF     */
#define SYS_CLOSE     4   /* close(fd)                                      */
#define SYS_EXIT      5   /* exit(code), does not return                    */
#define SYS_SLEEP     6   /* sleep(ticks), 100 ticks per second             */
#define SYS_FORK      7   /* fork() -> child pid in parent, 0 in child      */
#define SYS_EXEC      8   /* exec(path, argv), returns only on failure      */
#define SYS_WAITPID   9   /* waitpid(pid, &status, options) -> pid          */
#define SYS_GETPID    10  /* getpid()                                       */
#define SYS_SBRK      11  /* sbrk(increment) -> old break                   */
#define SYS_LSEEK     12  /* lseek(fd, offset, whence) -> new offset        */
#define SYS_STAT      13  /* stat(path, &st)                                */
#define SYS_KILL      14  /* kill(pid)                                      */
#define SYS_YIELD     15  /* yield()                                        */
#define SYS_TIME      16  /* time() -> milliseconds since boot              */
#define SYS_MKDIR     17  /* mkdir(path)                                    */
#define SYS_UNLINK    18  /* unlink(path): remove a file                    */
#define SYS_RMDIR     19  /* rmdir(path): remove an empty directory         */
#define SYS_RENAME    20  /* rename(old, new)                               */
#define SYS_CHDIR     21  /* chdir(path)                                    */
#define SYS_GETCWD    22  /* getcwd(buf, size) -> length                    */
#define SYS_READDIR   23  /* readdir(fd, &dirent) -> 1, 0 at the end       */
#define SYS_DUP       24  /* dup(fd) -> new fd                              */
#define SYS_DUP2      25  /* dup2(fd, newfd) -> newfd                       */
#define SYS_PIPE      26  /* pipe(int fds[2]): fds[0] read, fds[1] write    */
#define SYS_FSTAT     27  /* fstat(fd, &st)                                 */
#define SYS_CLOCK     28  /* clock() -> seconds since 1970 (RTC, local time) */

#define SYSCALL_COUNT 29

/* Standard file descriptors (the console) */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* open() flags */
#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_ACCMODE 0x003
#define O_CREAT   0x100
#define O_TRUNC   0x200
#define O_APPEND  0x400

/* lseek() whence */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* waitpid() options */
#define WNOHANG 1

/* stat() / dirent types */
#define S_IFREG 1   /* regular file */
#define S_IFCHR 2   /* character device (/dev/tty, /dev/null, ...) */
#define S_IFDIR 3   /* directory */
#define S_IFIFO 4   /* pipe */

struct stat
{
    unsigned int st_size;
    unsigned int st_mode;
    unsigned int st_ino;
    unsigned int st_nlink;
};

/* Longest file name (one path component), without the NUL */
#define NAME_MAX 27

/* readdir() */
struct dirent
{
    unsigned int d_ino;
    unsigned int d_type;            /* S_IF* */
    char d_name[NAME_MAX + 1];
};

/* Exit code of a process ended by kill() (128 + SIGKILL, as in shells) */
#define EXIT_KILLED 137

/* exec() limits: argument count and total bytes of argument strings */
#define EXEC_MAX_ARGS  16
#define EXEC_MAX_ARGLEN 1024

/* Longest path accepted by open/stat/exec/..., including the NUL */
#define PATH_MAX 128

#endif
