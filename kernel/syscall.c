#include "include/syscall.h"
#include "include/isr.h"
#include "include/printk.h"
#include "include/keyboard.h"
#include "include/vfs.h"
#include "include/fd.h"
#include "include/process.h"
#include "include/pit.h"
#include "include/scheduler.h"
#include "include/paging.h"

#define SYSCALL_ERR ((uint32_t)-1)
#define MAX_WRITE_LEN 4096
#define MAX_PATH_LEN  32    /* fd_table filename size, including NUL */

/*
 * User processes may pass any pointer: check it against their page tables
 * before the kernel touches it. Kernel threads are trusted.
 */
static int is_user(process_t *proc)
{
    return proc && proc->type == PROCESS_USER;
}

static int user_string_ok(process_t *proc, const char *s, uint32_t max)
{
    if (!is_user(proc))
        return 1;

    return vmm_user_strlen(proc->page_directory, s, max) >= 0;
}

static int user_buffer_ok(process_t *proc, void *buf, uint32_t len, int write)
{
    if (!is_user(proc))
        return 1;

    return vmm_user_range_ok(proc->page_directory, (uint32_t)buf, len, write);
}

void syscall_handler(registers_t *r)
{
    process_t *proc = process_current();

    if (proc && proc->type == PROCESS_USER)
        proc->trapframe = r;

    switch (r->eax)
    {
        case SYS_WRITE:
            if (!user_string_ok(proc, (const char *)r->ebx, MAX_WRITE_LEN))
            {
                r->eax = SYSCALL_ERR;
                break;
            }

            printk((const char *)r->ebx);
            r->eax = 0;
            break;

        case SYS_GETCHAR:
            r->eax = keyboard_getchar();
            break;

        case SYS_OPEN:
        {
            const char *path = (const char *)r->ebx;

            if (!user_string_ok(proc, path, MAX_PATH_LEN) || !vfs_exists(path))
            {
                r->eax = (uint32_t)-1;
                break;
            }

            r->eax = fd_alloc(path);

            break;
        }

        case SYS_READ:
        {
            int fd = (int)r->ebx;

            void *buffer = (void *)r->ecx;

            uint32_t size = r->edx;

            if (!fd_valid(fd) || !user_buffer_ok(proc, buffer, size, 1))
            {
                r->eax = (uint32_t)-1;
                break;
            }

            const char *filename = fd_filename(fd);

            r->eax = vfs_read(filename, buffer, size);

            break;
        }

        case SYS_CLOSE:
        {
            int fd = (int)r->ebx;

            if (!fd_valid(fd))
            {
                r->eax = (uint32_t)-1;
                break;
            }

            fd_close(fd);

            r->eax = 0;

            break;
        }

        case SYS_EXIT:
            /* isr128 sees the resched request and switches away for good */
            process_kill(proc, (int)r->ebx);
            break;

        case SYS_SLEEP:
        {
            process_t *proc = process_current();

            proc->wakeup_tick =
                pit_get_ticks() + r->ebx;

            proc->state = PROCESS_BLOCKED;

            scheduler_request_resched();

            break;
        }

        case SYS_YIELD:
            scheduler_request_resched();
            r->eax = 0;
            break;

        default:
            printk("Unknown syscall\n");
            break;
    }
}
