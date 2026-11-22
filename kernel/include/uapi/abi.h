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
#define SYS_SETPGID   29  /* setpgid(pid, pgid): 0 = self / pgid = pid      */
#define SYS_GETPGID   30  /* getpgid(pid) -> process group, 0 = self        */
#define SYS_TCSETPGRP 31  /* tcsetpgrp(pgid): group that gets Ctrl+C, 0 = none */
#define SYS_TCGETPGRP 32  /* tcgetpgrp() -> foreground group (0 = none)     */
#define SYS_PROCINFO  33  /* procinfo(slot, &info) -> 1, 0 if free, -1 past the end */
#define SYS_MEMINFO   34  /* meminfo(&info)                                 */
#define SYS_STATFS    35  /* statfs(path, &info)                            */
#define SYS_PCIINFO   36  /* pciinfo(index, &info) -> 0, -1 past the end    */
#define SYS_REBOOT    37  /* reboot(REBOOT_*), returns only on failure      */

#define SYSCALL_COUNT 38

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

/* Exit code of a process that wrote to a pipe nobody reads (128 + SIGPIPE) */
#define EXIT_PIPE 141

/* exec() limits: argument count and total bytes of argument strings */
#define EXEC_MAX_ARGS  16
#define EXEC_MAX_ARGLEN 1024

/* Longest path accepted by open/stat/exec/..., including the NUL */
#define PATH_MAX 128

/* reboot() */
#define REBOOT_RESTART  0
#define REBOOT_POWEROFF 1   /* ACPI S5 */

/* procinfo(): one process table slot */
struct procinfo
{
    unsigned int pid;
    unsigned int ppid;      /* 0 = no parent waiting for it */
    unsigned int pgid;      /* 0 for kernel threads */
    unsigned int user;      /* 1 = user process, 0 = kernel thread */
    char state[8];          /* "RUNNING", "READY", "SLEEP", "WAIT", "ZOMBIE" */
    char name[16];
};

/* meminfo(): physical frames, kernel heap and block cache */
struct meminfo
{
    unsigned int page_size;
    unsigned int total_frames;
    unsigned int free_frames;
    unsigned int heap_mapped;   /* bytes */
    unsigned int heap_used;
    unsigned int heap_free;
    unsigned int cache_sectors;
    unsigned int cache_hits;
    unsigned int cache_misses;
    unsigned int cache_writes;
    unsigned int cache_bulk;
};

/* statfs(): usage of the file system holding a path */
struct statfs
{
    unsigned int f_bsize;       /* block size in bytes */
    unsigned int f_blocks;
    unsigned int f_bfree;
    unsigned int f_files;       /* inodes */
    unsigned int f_ffree;
    char f_type[8];             /* "rxfs", "devfs" */
};

/* pciinfo(): one PCI function found at boot */
struct pciinfo
{
    unsigned char bus, slot, func, header_type;
    unsigned short vendor_id, device_id;
    unsigned char class_code, subclass, prog_if, revision;
    unsigned char irq_line, irq_pin, pad[2];
    unsigned int bar[6];
    char class_name[32];
    char vendor_name[24];       /* "" if unknown */
};

/*
 * getkey() codes: printable ASCII and control codes as usual (Ctrl+letter
 * = 1~26, Enter '
', Backspace '', Tab '	', Esc 27), these above 0x7F.
 */
#define KEY_LEFT   0x81
#define KEY_RIGHT  0x82
#define KEY_UP     0x83
#define KEY_DOWN   0x84
#define KEY_HOME   0x85
#define KEY_END    0x86
#define KEY_DELETE 0x87
#define KEY_PGUP   0x88
#define KEY_PGDN   0x89
#define KEY_INSERT 0x8A

/* Ctrl+letter arrives as its control code: KEY_CTRL('C') == 3. */
#define KEY_CTRL(c) ((c) & 0x1F)

#endif
