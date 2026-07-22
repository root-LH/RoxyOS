#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>
#include "types.h"

#define KERNEL_HEAP_START 0x100000
#define KERNEL_HEAP_SIZE  (1024 * 1024)

typedef struct block_header
{
    size_t size;
    uint8_t free;
} block_header_t;

void memory_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);

#endif