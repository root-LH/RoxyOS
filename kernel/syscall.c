#include "include/syscall.h"
#include "include/isr.h"
#include "include/printk.h"
#include "include/keyboard.h"
#include "include/vfs.h"
#include "include/fd.h"
#include "include/process.h"
#include "include/pit.h"
#include "include/scheduler.h"

void syscall_handler(registers_t *r)
{
    process_t *proc = process_current();

    if (proc && proc->type == PROCESS_USER)
        proc->trapframe = r;

    switch (r->eax)
    {
        case SYS_WRITE:
            printk((const char *)r->ebx);
            break;

        case SYS_GETCHAR:
            r->eax = keyboard_getchar();
            break;

        case SYS_OPEN:
        {
            const char *path = (const char *)r->ebx;

            if (!vfs_exists(path))
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

            if (!fd_valid(fd))
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
            process_exit();
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

        default:
            printk("Unknown syscall\n");
            break;
    }
}
