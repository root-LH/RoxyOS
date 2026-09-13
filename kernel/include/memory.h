#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>
#include "types.h"

#define KERNEL_HEAP_START 0x200000
#define KERNEL_HEAP_SIZE  (1024 * 1024)

#define PAGE_SIZE 4096

typedef struct block_header
{
    size_t size;
    uint8_t free;
    uint8_t padding[3];
} block_header_t;

struct e820_entry
{
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
} __attribute__((packed));

void memory_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);

// Physical Memory Manager
void pmm_init(uint32_t total_memory_mb);
void* pmm_alloc_frame(void);
void pmm_free_frame(void* frame_addr);

#endif