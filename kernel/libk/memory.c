#include "../include/memory.h"

static uint8_t *heap_ptr = (uint8_t *)KERNEL_HEAP_START;
static uint8_t *heap_end = (uint8_t *)(KERNEL_HEAP_START + KERNEL_HEAP_SIZE);

static size_t align4(size_t size)
{
    return (size + 3) & ~3;
}

void memory_init(void)
{
    heap_ptr = (uint8_t *)KERNEL_HEAP_START;
}

void *kmalloc(size_t size)
{
    if (size == 0)
        return NULL;

    size = align4(size);

    if (heap_ptr + size > heap_end)
        return NULL;

    void *ptr = heap_ptr;
    heap_ptr += size;

    return ptr;
}

void kfree(void *ptr)
{
    (void)ptr;
}