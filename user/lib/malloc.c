#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * First-fit allocator on the sbrk() heap. Blocks are kept in address order
 * (the heap only grows at the end), so neighbours in the list are
 * neighbours in memory and free blocks can be merged.
 */

typedef struct block
{
    size_t size;            /* payload bytes */
    int free;
    struct block *next;
} block_t;

#define ALIGN        8
#define HEADER       ((sizeof(block_t) + ALIGN - 1) & ~(ALIGN - 1))
#define MIN_SPLIT    16
#define GROW_MIN     4096

static block_t *head;
static block_t *tail;

static size_t align_up(size_t n)
{
    return (n + ALIGN - 1) & ~(size_t)(ALIGN - 1);
}

static void *payload(block_t *b)
{
    return (char *)b + HEADER;
}

static block_t *header_of(void *p)
{
    return (block_t *)((char *)p - HEADER);
}

/* Cuts b down to size bytes, turning the rest into a free block. */
static void split(block_t *b, size_t size)
{
    if (b->size < size + HEADER + MIN_SPLIT)
        return;

    block_t *rest = (block_t *)((char *)payload(b) + size);

    rest->size = b->size - size - HEADER;
    rest->free = 1;
    rest->next = b->next;

    b->size = size;
    b->next = rest;

    if (tail == b)
        tail = rest;
}

static block_t *grow(size_t size)
{
    size_t bytes = HEADER + size;

    if (bytes < GROW_MIN)
        bytes = GROW_MIN;

    bytes = (bytes + 4095) & ~(size_t)4095;

    void *mem = sbrk((int)bytes);

    if (mem == (void *)-1)
        return NULL;

    block_t *b = mem;

    b->size = bytes - HEADER;
    b->free = 1;
    b->next = NULL;

    if (tail)
        tail->next = b;
    else
        head = b;

    tail = b;

    return b;
}

void *malloc(size_t size)
{
    if (size == 0)
        return NULL;

    size = align_up(size);

    block_t *b = head;

    while (b && !(b->free && b->size >= size))
        b = b->next;

    if (!b)
    {
        /* A free last block only needs to grow by the difference. */
        block_t *last = tail;
        int extend = last && last->free;
        block_t *fresh = grow(extend ? size - last->size : size);

        if (!fresh)
            return NULL;

        if (extend && (char *)payload(last) + last->size == (char *)fresh)
        {
            last->size += HEADER + fresh->size;
            last->next = NULL;
            tail = last;
            b = last;
        }
        else if (fresh->size >= size)
        {
            b = fresh;
        }
        /* Someone else moved the break in between: start a fresh block. */
        else if (!(b = grow(size)))
        {
            return NULL;
        }
    }

    split(b, size);
    b->free = 0;

    return payload(b);
}

void free(void *ptr)
{
    if (!ptr)
        return;

    header_of(ptr)->free = 1;

    /* Merge every run of adjacent free blocks. */
    for (block_t *b = head; b && b->next; )
    {
        if (b->free && b->next->free)
        {
            if (tail == b->next)
                tail = b;

            b->size += HEADER + b->next->size;
            b->next = b->next->next;
        }
        else
        {
            b = b->next;
        }
    }
}

void *calloc(size_t count, size_t size)
{
    if (size && count > (size_t)-1 / size)
        return NULL;

    void *p = malloc(count * size);

    if (p)
        memset(p, 0, count * size);

    return p;
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr)
        return malloc(size);

    if (size == 0)
    {
        free(ptr);
        return NULL;
    }

    block_t *b = header_of(ptr);

    if (b->size >= size)
        return ptr;

    void *p = malloc(size);

    if (p)
    {
        memcpy(p, ptr, b->size);
        free(ptr);
    }

    return p;
}
