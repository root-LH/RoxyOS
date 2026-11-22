#include "../include/pipe.h"
#include "../include/memory.h"
#include "../include/string.h"
#include "../include/cpu.h"
#include "../include/process.h"

pipe_t *pipe_create(void)
{
    pipe_t *pipe = kmalloc(sizeof(pipe_t));

    if (!pipe)
        return 0;

    memset(pipe, 0, sizeof(pipe_t));
    pipe->readers = 1;
    pipe->writers = 1;

    return pipe;
}

int pipe_read(pipe_t *pipe, void *buf, uint32_t len)
{
    uint32_t flags = irq_save();

    while (pipe->count == 0)
    {
        if (pipe->writers == 0)
        {
            irq_restore(flags);
            return 0;
        }

        if (wait_queue_sleep(&pipe->readable, 1) < 0)
        {
            irq_restore(flags);
            return -1;
        }
    }

    uint32_t n = len < pipe->count ? len : pipe->count;

    for (uint32_t i = 0; i < n; i++)
        ((char *)buf)[i] = pipe->buf[(pipe->head + i) % PIPE_SIZE];

    pipe->head = (pipe->head + n) % PIPE_SIZE;
    pipe->count -= n;

    wait_queue_wake_all(&pipe->writable);

    irq_restore(flags);

    return (int)n;
}

int pipe_write(pipe_t *pipe, const void *buf, uint32_t len)
{
    uint32_t flags = irq_save();
    uint32_t done = 0;

    while (done < len)
    {
        if (pipe->readers == 0)
        {
            /*
             * Nobody will ever read this: like SIGPIPE, a user process
             * writing here ends (EXIT_PIPE) when its write() returns, so
             * "producer | head" does not keep the producer running.
             */
            process_t *self = process_current();

            if (self && self->type == PROCESS_USER && !self->killed)
            {
                self->killed = 1;
                self->kill_code = EXIT_PIPE;
            }
            break;
        }

        if (pipe->count == PIPE_SIZE)
        {
            if (wait_queue_sleep(&pipe->writable, 1) < 0)
                break;
            continue;
        }

        uint32_t space = PIPE_SIZE - pipe->count;
        uint32_t n = len - done < space ? len - done : space;
        uint32_t tail = (pipe->head + pipe->count) % PIPE_SIZE;

        for (uint32_t i = 0; i < n; i++)
            pipe->buf[(tail + i) % PIPE_SIZE] = ((const char *)buf)[done + i];

        pipe->count += n;
        done += n;

        wait_queue_wake_all(&pipe->readable);
    }

    irq_restore(flags);

    return done > 0 ? (int)done : -1;
}

void pipe_close(pipe_t *pipe, int write_end)
{
    uint32_t flags = irq_save();

    if (write_end)
        pipe->writers--;
    else
        pipe->readers--;

    /* Sleepers re-check: readers see EOF, writers see no readers. */
    wait_queue_wake_all(&pipe->readable);
    wait_queue_wake_all(&pipe->writable);

    int unused = pipe->readers == 0 && pipe->writers == 0;

    irq_restore(flags);

    if (unused)
        kfree(pipe);
}
