#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "isr.h"
#include "wait.h"
#include "uapi/abi.h"

#define MAX_PROCESS        16
#define PROCESS_STACK_SIZE 16384
#define PROCESS_MAX_FD     16
#define PROCESS_NAME_LEN   16

struct file;

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

    char name[PROCESS_NAME_LEN];

    process_state_t state;
    process_type_t type;

    /* Where the process resumes: the frame saved by its last trap. */
    registers_t *trapframe;

    uint8_t *kernel_stack;

    uint32_t wakeup_tick;

    int32_t exit_code;

    /* 0 = kernel directory (kernel threads) */
    uint32_t *page_directory;

    /* User heap: [heap_start, brk) grows with sbrk(). */
    uint32_t heap_start;
    uint32_t brk;

    /* Creator that will collect the exit code; 0 = reaped automatically. */
    struct process *parent;

    /*
     * Process group (user processes; 0 for kernel threads). A new program
     * starts its own group (pgid = pid), fork() keeps the parent's. Ctrl+C
     * goes to the whole foreground group (process_set_foreground).
     */
    uint32_t pgid;

    /* Wait queue state (see wait.h). */
    wait_queue_t *waiting_on;
    struct process *wait_next;
    int wait_interruptible;
    /* wait_queue_sleep_timeout(): tick to give up at (0 = never). */
    uint32_t wait_deadline;
    int wait_timed_out;

    /* Woken when one of this process's children exits. */
    wait_queue_t child_wq;

    /* Set by a kill request; the process exits before returning to user mode. */
    int killed;
    /* Exit code for that exit: EXIT_KILLED, or EXIT_PIPE (broken pipe). */
    int kill_code;

    /* File descriptors (see file.h); closed when the process exits. */
    struct file *files[PROCESS_MAX_FD];

    /* Working directory: a normalized absolute path (see vfs_abspath). */
    char cwd[PATH_MAX];

} process_t;

process_t *process_create_kernel(void (*entry)(void));

/*
 * Creates a ring 3 process running in page_directory (ownership passes to
 * the process; it is destroyed when the process is reaped).
 */
process_t *process_create_user(uint32_t *page_directory,
                               uint32_t entry, uint32_t user_esp);

/* Sets the name shown by ps (the last component of a path). */
void process_set_name(process_t *proc, const char *name);

/* The live (or zombie) process with this pid, or 0. */
process_t *process_find(uint32_t pid);

/*
 * fork(): copies the calling user process (address space, heap break,
 * descriptors). The child resumes from frame with eax = 0. Returns the
 * child's pid, or -1.
 */
int process_fork(registers_t *frame);

/*
 * Waits for a child of the caller to exit (pid -1 = any child), reaps it
 * and stores its exit code in *status (if not 0). Returns the child's pid,
 * 0 if WNOHANG is set and no child has exited yet, or -1 if there is no
 * such child (or the caller is killed while waiting).
 */
int process_waitpid(int pid, int *status, int options);

/* process_waitpid() on one child. Returns its exit code, or -1. */
int process_wait(process_t *child);

/*
 * kill(): ends user process pid with EXIT_KILLED. A process inside a
 * system call finishes or abandons it first (interruptible sleeps are
 * woken). Returns 0, or -1 if there is no such user process.
 */
int process_signal_kill(uint32_t pid);

/*
 * The process group that owns the console (the job the shell is running),
 * 0 = none. process_interrupt_foreground() (Ctrl+C, safe from IRQs) kills
 * every user process in it and returns how many; with none, Ctrl+C is
 * delivered as a key.
 */
void process_set_foreground(uint32_t pgid);
uint32_t process_get_foreground(void);
int process_interrupt_foreground(void);

/*
 * setpgid(): moves pid (0 = the caller) into group pgid (0 = its own pid).
 * Only the caller itself or one of its children. Returns 0 or -1.
 */
int process_setpgid(uint32_t pid, uint32_t pgid);

/*
 * Moves proc's heap break by increment bytes, mapping zeroed pages or
 * freeing them. Returns the old break, or -1.
 */
int32_t process_sbrk(process_t *proc, int32_t increment);

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
