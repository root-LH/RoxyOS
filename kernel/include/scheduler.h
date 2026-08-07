#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "process.h"

#define MAX_PROCESS 16

void scheduler_init(void);
void scheduler_set_idle(process_t *proc);
void scheduler_add(process_t *proc);
void scheduler_schedule(void);

void scheduler_tick(void);
int scheduler_need_resched(void);
void scheduler_clear_resched(void);
void scheduler_remove(process_t *proc);
void scheduler_wakeup(void);

#endif