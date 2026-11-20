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

    int size = vfs_size(path);

    if (size < 0)
        return -1;

    struct stat st;

    st.st_size = (uint32_t)size;
    st.st_mode = S_IFREG;

    return to_caller(r->ecx, &st, sizeof(st));
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

static const syscall_fn syscall_table[SYSCALL_COUNT] =
{
    [SYS_WRITE]   = sys_write,
    [SYS_GETCHAR] = sys_getchar,
    [SYS_OPEN]    = sys_open,
    [SYS_READ]    = sys_read,
    [SYS_CLOSE]   = sys_close,
    [SYS_EXIT]    = sys_exit,
    [SYS_SLEEP]   = sys_sleep,
    [SYS_GETPID]  = sys_getpid,
    [SYS_LSEEK]   = sys_lseek,
    [SYS_STAT]    = sys_stat,
    [SYS_YIELD]   = sys_yield,
    [SYS_TIME]    = sys_time,
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
        process_kill(proc, EXIT_KILLED);
}
