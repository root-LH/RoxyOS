#ifndef WAIT_H
#define WAIT_H

#include "types.h"

struct process;

/*
 * A list of processes sleeping until some event (a key press, a disk IRQ,
 * a child exiting). Waiters are linked through process_t::wait_next.
 */
typedef struct wait_queue
{
    struct process *head;
} wait_queue_t;

#define WAIT_QUEUE_INIT { 0 }

/*
 * Puts the current process to sleep on wq until wait_queue_wake_all().
 *
 * Call it with interrupts disabled, after checking the condition you wait
 * for, so a wakeup between the check and the sleep cannot be lost:
 *
 *     uint32_t flags = irq_save();
 *     while (!condition)
 *         if (wait_queue_sleep(&wq, 1) < 0) { ... killed ... }
 *     irq_restore(flags);
 *
 * Returns with interrupts still disabled. An interruptible sleep returns
 * -1 if the process was killed (see process_signal_kill), 0 otherwise.
 * Before the scheduler starts there is no process to block; the CPU just
 * waits for the next interrupt and the caller re-checks its condition.
 */
int wait_queue_sleep(wait_queue_t *wq, int interruptible);

/*
 * wait_queue_sleep() that also gives up after `ticks` timer ticks.
 * Returns 0 when woken, WAIT_TIMEOUT when the time ran out first, or -1
 * if interruptible and killed. Before the scheduler starts it waits for
 * one interrupt like wait_queue_sleep() (the caller re-checks).
 */
#define WAIT_TIMEOUT 1
int wait_queue_sleep_timeout(wait_queue_t *wq, int interruptible, uint32_t ticks);

/* Wakes waiters whose timeout has passed (timer IRQ, via the scheduler). */
void wait_queue_check_timeout(struct process *proc, uint32_t now);

/* Makes every waiter on wq runnable. Safe from IRQ handlers. */
void wait_queue_wake_all(wait_queue_t *wq);

/* Removes proc from the queue it sleeps on (if any) without waking it. */
void wait_queue_cancel(struct process *proc);

/*
 * Sleeping mutex for code that may block while holding it (disk I/O).
 * Uninterruptible; must not be used from IRQ handlers.
 */
typedef struct kmutex
{
    volatile int locked;
    struct process *owner;
    wait_queue_t waiters;
} kmutex_t;

#define KMUTEX_INIT { 0, 0, WAIT_QUEUE_INIT }

void kmutex_lock(kmutex_t *m);
void kmutex_unlock(kmutex_t *m);

#endif
