#include "include/scheduler.h"
#include "include/pit.h"
#include "include/wait.h"

#include "include/printk.h"

static process_t *processes[MAX_PROCESS];

static int process_count = 0;
static int current = 0;

static volatile int need_resched = 0;
static uint32_t scheduler_ticks = 0;

static process_t *idle_process = 0;

#define TIME_SLICE 10

void scheduler_init(void)
{
    process_count = 0;
    current = 0;
}

void scheduler_set_idle(process_t *proc)
{
    idle_process = proc;
}

void scheduler_add(process_t *proc)
{
    if (process_count >= MAX_PROCESS)
        return;

    processes[process_count++] = proc;
}

process_t *scheduler_schedule(void)
{
    process_t *current_proc = process_current();

    int start = -1;

    for (int i = 0; i < process_count; i++)
    {
        if (processes[i] == current_proc)
        {
            start = i;
            break;
        }
    }

    if (start == -1)
        return idle_process;

    for (int i = 1; i <= process_count; i++)
    {
        int next = (start + i) % process_count;

        process_t *proc = processes[next];

        if (proc == current_proc)
            continue;

        if (proc->state != PROCESS_READY)
            continue;

        current = next;

        return proc;
    }

    return idle_process;
}

void scheduler_tick(void)
{
    scheduler_ticks++;

    if (scheduler_ticks >= TIME_SLICE)
    {
        scheduler_ticks = 0;

        need_resched = 1;
    }
}

int scheduler_need_resched(void)
{
    return need_resched;
}

void scheduler_clear_resched(void)
{
    need_resched = 0;
}

void scheduler_remove(process_t *proc)
{
    int index = -1;

    for (int i = 0; i < process_count; i++)
    {
        if (processes[i] == proc)
        {
            index = i;
            break;
        }
    }

    if (index == -1)
        return;

    for (int i = index; i < process_count - 1; i++)
        processes[i] = processes[i + 1];

    process_count--;

    if (current >= process_count)
        current = 0;
}

void scheduler_wakeup(void)
{
    uint32_t now = pit_get_ticks();

    for (int i = 0; i < process_count; i++)
    {
        process_t *proc = processes[i];

        if (proc->state == PROCESS_WAITING)
        {
            wait_queue_check_timeout(proc, now);
            continue;
        }

        if (proc->state != PROCESS_BLOCKED)
            continue;

        if ((int32_t)(now - proc->wakeup_tick) >= 0)
        {
            proc->state = PROCESS_READY;
            need_resched = 1;
        }
    }
}

void scheduler_request_resched(void)
{
    need_resched = 1;
}