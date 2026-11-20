#ifndef PIPE_H
#define PIPE_H

#include "types.h"
#include "wait.h"

#define PIPE_SIZE 4096

/*
 * A one-way byte channel between processes (a ring buffer). Readers sleep
 * while it is empty, writers while it is full. Once every write end is
 * closed, reads return 0 (end of file); once every read end is closed,
 * writes fail.
 */
typedef struct pipe
{
    char buf[PIPE_SIZE];
    uint32_t head;      /* next byte to read */
    uint32_t count;     /* bytes in buf */
    int readers;        /* open read ends (file_t, not descriptors) */
    int writers;
    wait_queue_t readable;
    wait_queue_t writable;
} pipe_t;

/* A new pipe with one read and one write end open. */
pipe_t *pipe_create(void);

/* Blocks until data (or EOF). Returns bytes read, 0 at EOF, -1 if killed. */
int pipe_read(pipe_t *pipe, void *buf, uint32_t len);

/* Blocks until all of buf fits. Returns len, or a short count / -1 when the readers go away or the writer is killed. */
int pipe_write(pipe_t *pipe, const void *buf, uint32_t len);

/* Closes one end; the pipe is freed with its last end. Never sleeps. */
void pipe_close(pipe_t *pipe, int write_end);

#endif
