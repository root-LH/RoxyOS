#include "include/wait.h"
#include "include/cpu.h"
#include "include/process.h"
#include "include/scheduler.h"

int wait_queue_sleep(wait_queue_t *wq, int interruptible)
{
    uint32_t flags = irq_save();
    process_t *self = process_current();

    if (!self)
    {
        /* Early boot: nothing to switch to, wait for the next interrupt. */
        asm volatile("sti; hlt; cli" : : : "memory");
        irq_restore(flags);
        return 0;
    }

    if (interruptible && self->killed)
    {
        irq_restore(flags);
        return -1;
    }

    self->wait_next = wq->head;
    wq->head = self;
    self->waiting_on = wq;
    self->wait_interruptible = interruptible;
    self->state = PROCESS_WAITING;

    process_block();

    /* The waker (or process_signal_kill) already took us off the queue. */
    irq_restore(flags);

    return (interruptible && self->killed) ? -1 : 0;
}

void wait_queue_wake_all(wait_queue_t *wq)
{
    uint32_t flags = irq_save();
    process_t *proc = wq->head;

    wq->head = 0;

    while (proc)
    {
        process_t *next = proc->wait_next;

        proc->wait_next = 0;
        proc->waiting_on = 0;

        if (proc->state == PROCESS_WAITING)
        {
            proc->state = PROCESS_READY;
            scheduler_request_resched();
        }

        proc = next;
    }

    irq_restore(flags);
}

void wait_queue_cancel(process_t *proc)
{
    uint32_t flags = irq_save();
    wait_queue_t *wq = proc->waiting_on;

    if (wq)
    {
        process_t **link = &wq->head;

        while (*link && *link != proc)
            link = &(*link)->wait_next;

        if (*link)
            *link = proc->wait_next;

        proc->wait_next = 0;
        proc->waiting_on = 0;
    }

    irq_restore(flags);
}

void kmutex_lock(kmutex_t *m)
{
    uint32_t flags = irq_save();

    while (m->locked)
        wait_queue_sleep(&m->waiters, 0);

    m->locked = 1;
    m->owner = process_current();

    irq_restore(flags);
}

void kmutex_unlock(kmutex_t *m)
{
    uint32_t flags = irq_save();

    m->locked = 0;
    m->owner = 0;
    wait_queue_wake_all(&m->waiters);

    irq_restore(flags);
}
