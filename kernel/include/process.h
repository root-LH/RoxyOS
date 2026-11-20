#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "isr.h"
#include "wait.h"

#define MAX_PROCESS        16
#define PROCESS_STACK_SIZE 8192

typedef enum
{
    PROCESS_UNUSED = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,    /* sleeping until wakeup_tick */
    PROCESS_EXITED,     /* zombie: finished, slot not yet reaped */
    PROCESS_WAITING     /* sleeping on a wait queue */
} process_state_t;

typedef enum
{
    PROCESS_KERNEL = 0,
    PROCESS_USER
} process_type_t;

typedef struct process
{
    uint32_t pid;

    process_state_t state;
    process_type_t type;

    /* Where the process resumes: the frame saved by its last trap. */
    registers_t *trapframe;

    uint8_t *kernel_stack;

    uint32_t wakeup_tick;

    int32_t exit_code;

    /* 0 = kernel directory (kernel threads) */
    uint32_t *page_directory;

    /* Creator that will collect the exit code; 0 = reaped automatically. */
    struct process *parent;

    /* Wait queue state (see wait.h). */
    wait_queue_t *waiting_on;
    struct process *wait_next;
    int wait_interruptible;

    /* Woken when one of this process's children exits. */
    wait_queue_t child_wq;

    /* Set by a kill request; the process exits before returning to user mode. */
    int killed;

} process_t;

process_t *process_create_kernel(void (*entry)(void));

/*
 * Creates a ring 3 process running in page_directory (ownership passes to
 * the process; it is destroyed when the process is reaped).
 */
process_t *process_create_user(uint32_t *page_directory,
                               uint32_t entry, uint32_t user_esp);

/*
 * Blocks until child (created by the caller) exits, reaps it and returns
 * its exit code. Returns -1 if child is not a live child of the caller.
 */
int process_wait(process_t *child);

process_t *process_current(void);

void process_set_current(process_t *proc);

/*
 * Switches away from the current process right now, through the syscall
 * gate. Set the process state first: RUNNING/READY yields the CPU,
 * BLOCKED/WAITING sleeps until something makes it READY again.
 */
void process_block(void);

/* Gives the CPU to the next ready process. */
void process_yield(void);

/* Terminates the calling process. Never returns. */
void process_exit(int code) __attribute__((noreturn));

/*
 * Marks proc as exited (zombie). If proc is the current process, a
 * reschedule is requested; the caller must return to the trap path.
 */
void process_kill(process_t *proc, int code);

/* Frees every exited process slot that is no longer running. */
void process_reap(void);

/* Returns the process in table slot i (any state), or 0 if out of range. */
process_t *process_get_slot(int i);

const char *process_state_name(process_state_t state);

void process_sleep(uint32_t ticks);

registers_t *process_reschedule_from_trap(registers_t *frame);

void process_start(process_t *first) __attribute__((noreturn));

#endif
