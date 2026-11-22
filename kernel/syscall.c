#include "include/syscall.h"
#include "include/isr.h"
#include "include/printk.h"
#include "include/keyboard.h"
#include "include/vfs.h"
#include "include/file.h"
#include "include/process.h"
#include "include/pit.h"
#include "include/scheduler.h"
#include "include/paging.h"
#include "include/memory.h"
#include "include/string.h"
#include "include/elf_loader.h"
#include "include/rtc.h"
#include "include/bcache.h"
#include "include/pci.h"
#include "include/acpi.h"
#include "include/cpu.h"

/* Largest transfer of one read/write call; callers loop for more. */
#define IO_CHUNK 4096

typedef int32_t (*syscall_fn)(registers_t *r);

static int is_user(process_t *proc)
{
    return proc && proc->type == PROCESS_USER;
}

/*
 * User processes may pass any pointer, so their memory is only touched
 * through copy_from_user/copy_to_user (checked against their page tables).
 * Kernel threads are trusted and pass kernel pointers.
 */
static int from_caller(void *dst, uint32_t src, uint32_t len)
{
    process_t *proc = process_current();

    if (!is_user(proc))
    {
        memcpy(dst, (const void *)src, len);
        return 0;
    }

    return copy_from_user(proc->page_directory, dst, src, len);
}

static int to_caller(uint32_t dst, const void *src, uint32_t len)
{
    process_t *proc = process_current();

    if (!is_user(proc))
    {
        memcpy((void *)dst, src, len);
        return 0;
    }

    return copy_to_user(proc->page_directory, dst, src, len);
}

/* Copies a path argument into buf[PATH_MAX]. Returns 0 or -1. */
static int path_from_caller(char *buf, uint32_t src)
{
    process_t *proc = process_current();

    if (!is_user(proc))
    {
        if (strlen((const char *)src) >= PATH_MAX)
            return -1;

        strcpy(buf, (const char *)src);
        return 0;
    }

    return strncpy_from_user(proc->page_directory, buf, src, PATH_MAX) < 0 ? -1 : 0;
}

static int32_t sys_write(registers_t *r)
{
    file_t *file = fd_get(process_current(), (int)r->ebx);
    uint32_t len = r->edx;

    if (!file)
        return -1;

    if (len > IO_CHUNK)
        len = IO_CHUNK;

    if (len == 0)
        return 0;

    char *buf = kmalloc(len);

    if (!buf)
        return -1;

    int32_t n = -1;

    if (from_caller(buf, r->ecx, len) == 0)
        n = file_write(file, buf, len);

    kfree(buf);

    return n;
}

static int32_t sys_read(registers_t *r)
{
    process_t *proc = process_current();
    file_t *file = fd_get(proc, (int)r->ebx);
    uint32_t len = r->edx;

    if (!file)
        return -1;

    if (len > IO_CHUNK)
        len = IO_CHUNK;

    if (len == 0)
        return 0;

    /* Fail before consuming any input if the result could not be stored. */
    if (is_user(proc) &&
        !vmm_user_range_ok(proc->page_directory, r->ecx, len, 1))
        return -1;

    char *buf = kmalloc(len);

    if (!buf)
        return -1;

    int32_t n = file_read(file, buf, len);

    if (n > 0 && to_caller(r->ecx, buf, (uint32_t)n) != 0)
        n = -1;

    kfree(buf);

    return n;
}

static int32_t sys_getchar(registers_t *r)
{
    (void)r;

    return keyboard_read(1);
}

static int32_t sys_open(registers_t *r)
{
    char path[PATH_MAX];

    if (path_from_caller(path, r->ebx) != 0)
        return -1;

    file_t *file = file_open(path, (int)r->ecx);

    if (!file)
        return -1;

    int fd = fd_install(process_current(), file);

    if (fd < 0)
        file_close(file);

    return fd;
}

static int32_t sys_close(registers_t *r)
{
    return fd_close(process_current(), (int)r->ebx);
}

static int32_t sys_lseek(registers_t *r)
{
    file_t *file = fd_get(process_current(), (int)r->ebx);

    if (!file)
        return -1;

    return file_seek(file, (int32_t)r->ecx, (int)r->edx);
}

static int32_t sys_stat(registers_t *r)
{
    char path[PATH_MAX];

    if (path_from_caller(path, r->ebx) != 0)
        return -1;

    struct stat st;

    if (vfs_stat(path, &st) != 0)
        return -1;

    return to_caller(r->ecx, &st, sizeof(st));
}

static int32_t sys_fstat(registers_t *r)
{
    file_t *file = fd_get(process_current(), (int)r->ebx);
    struct stat st;

    if (!file || file_stat(file, &st) != 0)
        return -1;

    return to_caller(r->ecx, &st, sizeof(st));
}

/* Calls op on the path in ebx. */
static int32_t path_call(registers_t *r, int (*op)(const char *))
{
    char path[PATH_MAX];

    if (path_from_caller(path, r->ebx) != 0)
        return -1;

    return op(path);
}

static int32_t sys_mkdir(registers_t *r)
{
    return path_call(r, vfs_mkdir);
}

static int32_t sys_unlink(registers_t *r)
{
    return path_call(r, vfs_unlink);
}

static int32_t sys_rmdir(registers_t *r)
{
    return path_call(r, vfs_rmdir);
}

static int32_t sys_chdir(registers_t *r)
{
    return path_call(r, vfs_chdir);
}

static int32_t sys_rename(registers_t *r)
{
    char old_path[PATH_MAX], new_path[PATH_MAX];

    if (path_from_caller(old_path, r->ebx) != 0 || path_from_caller(new_path, r->ecx) != 0)
        return -1;

    return vfs_rename(old_path, new_path);
}

/* getcwd(buf, size): copies the NUL-terminated path, returns its length. */
static int32_t sys_getcwd(registers_t *r)
{
    const char *cwd = vfs_getcwd();
    uint32_t len = strlen(cwd);

    if (r->ecx < len + 1 || to_caller(r->ebx, cwd, len + 1) != 0)
        return -1;

    return (int32_t)len;
}

static int32_t sys_readdir(registers_t *r)
{
    file_t *file = fd_get(process_current(), (int)r->ebx);
    struct dirent entry;

    if (!file)
        return -1;

    memset(&entry, 0, sizeof(entry));

    int n = file_readdir(file, &entry);

    if (n == 1 && to_caller(r->ecx, &entry, sizeof(entry)) != 0)
        return -1;

    return n;
}

static int32_t sys_exit(registers_t *r)
{
    /* isr128 sees the resched request and switches away for good. */
    process_kill(process_current(), (int)r->ebx);

    return 0;
}

static int32_t sys_sleep(registers_t *r)
{
    process_t *proc = process_current();

    proc->wakeup_tick = pit_get_ticks() + r->ebx;
    proc->state = PROCESS_BLOCKED;

    /* Switched away on the way out of isr128; scheduler_wakeup() resumes us. */
    scheduler_request_resched();

    return 0;
}

static int32_t sys_getpid(registers_t *r)
{
    (void)r;

    return (int32_t)process_current()->pid;
}

static int32_t sys_yield(registers_t *r)
{
    (void)r;

    scheduler_request_resched();

    return 0;
}

static int32_t sys_time(registers_t *r)
{
    (void)r;

    return (int32_t)(pit_get_ticks() * (1000 / PIT_HZ));
}

static int32_t sys_clock(registers_t *r)
{
    (void)r;

    return (int32_t)rtc_now();
}

static int32_t sys_fork(registers_t *r)
{
    return process_fork(r);
}

/* exec(path, argv): argv is a NULL-terminated array, or NULL for { path }. */
static int32_t sys_exec(registers_t *r)
{
    process_t *proc = process_current();
    char path[PATH_MAX];

    if (!is_user(proc) || path_from_caller(path, r->ebx) != 0)
        return -1;

    char *argv[EXEC_MAX_ARGS];
    char *strings = kmalloc(EXEC_MAX_ARGLEN);
    int argc = 0;
    uint32_t used = 0;
    int32_t result = -1;

    if (!strings)
        return -1;

    for (uint32_t uargv = r->ecx; uargv; argc++)
    {
        uint32_t arg;

        if (from_caller(&arg, uargv + argc * sizeof(uint32_t), sizeof(arg)) != 0)
            goto out;

        if (!arg)
            break;

        if (argc == EXEC_MAX_ARGS)
            goto out;

        int len = strncpy_from_user(proc->page_directory, strings + used, arg,
                                    EXEC_MAX_ARGLEN - used);

        if (len < 0)
            goto out;

        argv[argc] = strings + used;
        used += len + 1;
    }

    if (argc == 0)
        argv[argc++] = path;

    result = elf_exec(r, path, argc, argv);

out:
    kfree(strings);

    return result;
}

static int32_t sys_waitpid(registers_t *r)
{
    int status;
    int pid = process_waitpid((int)r->ebx, &status, (int)r->edx);

    if (pid > 0 && r->ecx && to_caller(r->ecx, &status, sizeof(status)) != 0)
        return -1;

    return pid;
}

static int32_t sys_sbrk(registers_t *r)
{
    process_t *proc = process_current();

    if (!is_user(proc))
        return -1;

    return process_sbrk(proc, (int32_t)r->ebx);
}

static int32_t sys_kill(registers_t *r)
{
    return process_signal_kill(r->ebx);
}

static int32_t sys_dup(registers_t *r)
{
    process_t *proc = process_current();
    file_t *file = fd_get(proc, (int)r->ebx);

    if (!file)
        return -1;

    int fd = fd_install(proc, file_dup(file));

    if (fd < 0)
        file_close(file);

    return fd;
}

/* dup2(fd, newfd): newfd (closed first if open) becomes a copy of fd. */
static int32_t sys_dup2(registers_t *r)
{
    process_t *proc = process_current();
    file_t *file = fd_get(proc, (int)r->ebx);
    int newfd = (int)r->ecx;

    if (!file || newfd < 0 || newfd >= PROCESS_MAX_FD)
        return -1;

    if (newfd == (int)r->ebx)
        return newfd;

    fd_close(proc, newfd);
    proc->files[newfd] = file_dup(file);

    return newfd;
}

/* pipe(int fds[2]): fds[0] is the read end, fds[1] the write end. */
static int32_t sys_pipe(registers_t *r)
{
    process_t *proc = process_current();
    file_t *read_end, *write_end;

    if (file_pipe(&read_end, &write_end) != 0)
        return -1;

    int fds[2] = { fd_install(proc, read_end), -1 };

    if (fds[0] >= 0)
        fds[1] = fd_install(proc, write_end);

    if (fds[1] < 0 || to_caller(r->ebx, fds, sizeof(fds)) != 0)
    {
        if (fds[0] >= 0)
            fd_close(proc, fds[0]);
        else
            file_close(read_end);

        if (fds[1] >= 0)
            fd_close(proc, fds[1]);
        else
            file_close(write_end);

        return -1;
    }

    return 0;
}

static int32_t sys_setpgid(registers_t *r)
{
    return process_setpgid(r->ebx, r->ecx);
}

static int32_t sys_getpgid(registers_t *r)
{
    process_t *proc = r->ebx ? process_find(r->ebx) : process_current();

    if (!proc || proc->type != PROCESS_USER)
        return -1;

    return (int32_t)proc->pgid;
}

static int32_t sys_tcsetpgrp(registers_t *r)
{
    process_set_foreground(r->ebx);

    return 0;
}

static int32_t sys_tcgetpgrp(registers_t *r)
{
    (void)r;

    return (int32_t)process_get_foreground();
}

/* Copies at most size - 1 bytes of src and always terminates dst. */
static void copy_name(char *dst, const char *src, uint32_t size)
{
    uint32_t i = 0;

    for (; src && src[i] && i < size - 1; i++)
        dst[i] = src[i];

    dst[i] = '\0';
}

static int32_t sys_procinfo(registers_t *r)
{
    int slot = (int)r->ebx;

    if (slot < 0 || slot >= MAX_PROCESS)
        return -1;

    struct procinfo info;
    uint32_t flags = irq_save();
    process_t *proc = process_get_slot(slot);

    memset(&info, 0, sizeof(info));

    int used = proc->state != PROCESS_UNUSED;

    if (used)
    {
        info.pid = proc->pid;
        info.ppid = proc->parent ? proc->parent->pid : 0;
        info.pgid = proc->pgid;
        info.user = proc->type == PROCESS_USER;
        copy_name(info.state, process_state_name(proc->state), sizeof(info.state));
        copy_name(info.name, proc->name, sizeof(info.name));
    }

    irq_restore(flags);

    if (used && to_caller(r->ecx, &info, sizeof(info)) != 0)
        return -1;

    return used;
}

static int32_t sys_meminfo(registers_t *r)
{
    struct meminfo info;
    heap_stats_t heap;
    bcache_stats_t cache;

    heap_get_stats(&heap);
    bcache_get_stats(&cache);

    info.page_size = PAGE_SIZE;
    info.total_frames = pmm_total_frame_count();
    info.free_frames = pmm_free_frame_count();
    info.heap_mapped = heap.mapped;
    info.heap_used = heap.used;
    info.heap_free = heap.free;
    info.cache_sectors = BCACHE_ENTRIES;
    info.cache_hits = cache.hits;
    info.cache_misses = cache.misses;
    info.cache_writes = cache.writes;
    info.cache_bulk = cache.bulk;

    return to_caller(r->ebx, &info, sizeof(info));
}

static int32_t sys_statfs(registers_t *r)
{
    char path[PATH_MAX];
    inode_t *inode;
    statfs_t fs;

    if (path_from_caller(path, r->ebx) != 0 || vfs_lookup(path, &inode) != 0)
        return -1;

    struct statfs info;

    memset(&info, 0, sizeof(info));
    copy_name(info.f_type, inode->fs->type, sizeof(info.f_type));

    int ok = inode->fs->ops->statfs(inode->fs, &fs) == 0;

    inode_put(inode);

    if (!ok)
        return -1;

    info.f_bsize = fs.block_size;
    info.f_blocks = fs.total_blocks;
    info.f_bfree = fs.free_blocks;
    info.f_files = fs.total_inodes;
    info.f_ffree = fs.free_inodes;

    return to_caller(r->ecx, &info, sizeof(info));
}

static int32_t sys_pciinfo(registers_t *r)
{
    const pci_device_t *d = pci_get_device((int)r->ebx);

    if ((int)r->ebx < 0 || !d)
        return -1;

    struct pciinfo info;

    memset(&info, 0, sizeof(info));

    info.bus = d->bus;
    info.slot = d->slot;
    info.func = d->func;
    info.header_type = d->header_type;
    info.vendor_id = d->vendor_id;
    info.device_id = d->device_id;
    info.class_code = d->class_code;
    info.subclass = d->subclass;
    info.prog_if = d->prog_if;
    info.revision = d->revision;
    info.irq_line = d->irq_line;
    info.irq_pin = d->irq_pin;
    memcpy(info.bar, d->bar, sizeof(info.bar));
    copy_name(info.class_name, pci_class_name(d), sizeof(info.class_name));
    copy_name(info.vendor_name, pci_vendor_name(d->vendor_id), sizeof(info.vendor_name));

    return to_caller(r->ecx, &info, sizeof(info));
}

static int32_t sys_reboot(registers_t *r)
{
    switch (r->ebx)
    {
        case REBOOT_RESTART:
            kprintf("Rebooting...\n");
            keyboard_reboot();

        case REBOOT_POWEROFF:
            kprintf("Powering off...\n");
            acpi_poweroff();
            kprintf("shutdown: the machine did not power off\n");
            return -1;

        default:
            return -1;
    }
}

static const syscall_fn syscall_table[SYSCALL_COUNT] =
{
    [SYS_WRITE]   = sys_write,
    [SYS_GETCHAR] = sys_getchar,
    [SYS_OPEN]    = sys_open,
    [SYS_READ]    = sys_read,
    [SYS_CLOSE]   = sys_close,
    [SYS_EXIT]    = sys_exit,
    [SYS_SLEEP]   = sys_sleep,
    [SYS_FORK]    = sys_fork,
    [SYS_EXEC]    = sys_exec,
    [SYS_WAITPID] = sys_waitpid,
    [SYS_GETPID]  = sys_getpid,
    [SYS_SBRK]    = sys_sbrk,
    [SYS_LSEEK]   = sys_lseek,
    [SYS_STAT]    = sys_stat,
    [SYS_KILL]    = sys_kill,
    [SYS_YIELD]   = sys_yield,
    [SYS_TIME]    = sys_time,
    [SYS_MKDIR]   = sys_mkdir,
    [SYS_UNLINK]  = sys_unlink,
    [SYS_RMDIR]   = sys_rmdir,
    [SYS_RENAME]  = sys_rename,
    [SYS_CHDIR]   = sys_chdir,
    [SYS_GETCWD]  = sys_getcwd,
    [SYS_READDIR] = sys_readdir,
    [SYS_DUP]     = sys_dup,
    [SYS_DUP2]    = sys_dup2,
    [SYS_PIPE]    = sys_pipe,
    [SYS_FSTAT]   = sys_fstat,
    [SYS_CLOCK]   = sys_clock,
    [SYS_SETPGID]   = sys_setpgid,
    [SYS_GETPGID]   = sys_getpgid,
    [SYS_TCSETPGRP] = sys_tcsetpgrp,
    [SYS_TCGETPGRP] = sys_tcgetpgrp,
    [SYS_PROCINFO]  = sys_procinfo,
    [SYS_MEMINFO]   = sys_meminfo,
    [SYS_STATFS]    = sys_statfs,
    [SYS_PCIINFO]   = sys_pciinfo,
    [SYS_REBOOT]    = sys_reboot,
};

void syscall_handler(registers_t *r)
{
    process_t *proc = process_current();

    if (is_user(proc))
        proc->trapframe = r;

    if (r->eax >= SYSCALL_COUNT || !syscall_table[r->eax])
    {
        klog("[syscall] pid %u: unknown syscall %u\n",
             proc ? proc->pid : 0, r->eax);
        r->eax = (uint32_t)-1;
        return;
    }

    r->eax = (uint32_t)syscall_table[r->eax](r);

    /*
     * A process killed while inside the kernel exits here, on its way back
     * to user mode, once it no longer holds any kernel state.
     */
    if (is_user(proc) && (r->cs & 3) == 3 && proc->killed &&
        proc->state != PROCESS_EXITED)
        process_kill(proc, proc->kill_code ? proc->kill_code : EXIT_KILLED);
}
