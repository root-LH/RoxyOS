#include "include/wait.h"
#include "include/cpu.h"
#include "include/process.h"
#include "include/scheduler.h"

#include "include/pit.h"

static int sleep_on(wait_queue_t *wq, int interruptible, uint32_t ticks)
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
    self->wait_timed_out = 0;
    self->wait_deadline = 0;

    if (ticks)
    {
        /* 0 means "no deadline", so a deadline that lands on tick 0 moves by one. */
        self->wait_deadline = pit_get_ticks() + ticks;

        if (self->wait_deadline == 0)
            self->wait_deadline = 1;
    }
    self->state = PROCESS_WAITING;

    process_block();

    /* The waker (or process_signal_kill, or the timeout) took us off the queue. */
    self->wait_deadline = 0;
    irq_restore(flags);

    if (interruptible && self->killed)
        return -1;

    return self->wait_timed_out ? WAIT_TIMEOUT : 0;
}

int wait_queue_sleep(wait_queue_t *wq, int interruptible)
{
    return sleep_on(wq, interruptible, 0);
}

int wait_queue_sleep_timeout(wait_queue_t *wq, int interruptible, uint32_t ticks)
{
    return sleep_on(wq, interruptible, ticks ? ticks : 1);
}

void wait_queue_check_timeout(process_t *proc, uint32_t now)
{
    if (proc->state != PROCESS_WAITING || !proc->wait_deadline)
        return;

    if ((int32_t)(now - proc->wait_deadline) < 0)
        return;

    wait_queue_cancel(proc);
    proc->wait_deadline = 0;
    proc->wait_timed_out = 1;
    proc->state = PROCESS_READY;
    scheduler_request_resched();
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
