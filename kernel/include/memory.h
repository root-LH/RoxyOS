#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>
#include "types.h"

/* Kernel heap: virtual, in the shared kernel half (see paging.h). */
#define KERNEL_HEAP_START   0xD0000000
#define KERNEL_HEAP_MAX     (256 * 1024 * 1024)
#define KERNEL_HEAP_INITIAL (64 * 1024)

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

/* Stage2 (boot/stage2.asm) stores the E820 map here (physical). */
#define E820_COUNT_ADDR  0x9000
#define E820_MAP_ADDR    0x9004
#define E820_MAX_ENTRIES 32
#define E820_USABLE      1

typedef struct
{
    uint32_t mapped;    /* bytes mapped for the heap */
    uint32_t used;      /* bytes in allocated blocks */
    uint32_t free;      /* bytes in free blocks + mapped tail */
    uint32_t blocks;
} heap_stats_t;

/* Kernel heap (call heap_init after paging_init) */
void heap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
/* Grows the heap by increment bytes (page-rounded). Returns the old break or NULL. */
void *kbrk(size_t increment);
void heap_get_stats(heap_stats_t *stats);

/* Physical Memory Manager: sized from the E820 map */
void pmm_init(void);
void* pmm_alloc_frame(void);
void pmm_free_frame(void* frame_addr);
uint32_t pmm_total_frame_count(void);
uint32_t pmm_free_frame_count(void);
const struct e820_entry *pmm_memory_map(uint32_t *count);

#endif