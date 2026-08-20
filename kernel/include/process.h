#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "isr.h"

#define MAX_PROCESS        16
#define PROCESS_STACK_SIZE 4096
#define USER_STACK_SIZE 4096

typedef enum
{
    PROCESS_UNUSED = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_EXITED
} process_state_t;

typedef enum
{
    PROCESS_KERNEL = 0,
    PROCESS_USER
} process_type_t;

typedef struct context
{
    uint32_t edi;
    uint32_t esi;
    uint32_t ebx;
    uint32_t ebp;
} context_t;

typedef struct user_context
{
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t esp;
    uint32_t ss;
} user_context_t;

typedef struct process
{
    uint32_t pid;

    process_state_t state;
    process_type_t type;

    context_t *context;
    registers_t *trapframe;
    user_context_t *user_context;

    void (*entry)(void);

    uint8_t *kernel_stack;
    uint8_t *user_stack;

    uint32_t user_stack_top;

    uint32_t wakeup_tick;

} process_t;

process_t *process_create_kernel(void (*entry)(void));

process_t *process_create_user(void (*entry)(void));

process_t *process_current(void);

void process_set_current(process_t *proc);

void process_switch(process_t *next);

void process_switch_asm(context_t **old, context_t *new);

void process_yield(void);

void process_exit(void);

void process_sleep(uint32_t ticks);

void process_enter_user(process_t *proc);

registers_t *process_reschedule_from_trap(registers_t *frame);

#endif