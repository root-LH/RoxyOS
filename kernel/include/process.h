#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

#define MAX_PROCESS        16
#define PROCESS_STACK_SIZE 4096

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

typedef struct process
{
    uint32_t pid;

    process_state_t state;
    process_type_t type;

    context_t *context;

    uint32_t wakeup_tick;

    uint8_t stack[PROCESS_STACK_SIZE];

} process_t;

process_t *process_create_kernel(void (*entry)(void));

process_t *process_current(void);

void process_set_current(process_t *proc);

void process_switch(process_t *next);

void process_switch_asm(context_t **old, context_t *new);

void process_yield(void);

void process_exit(void);

void process_sleep(uint32_t ticks);

#endif