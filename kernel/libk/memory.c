#include "../include/memory.h"

static uint8_t *heap_ptr = (uint8_t *)KERNEL_HEAP_START;
static uint8_t *heap_end = (uint8_t *)(KERNEL_HEAP_START + KERNEL_HEAP_SIZE);

static size_t align4(size_t size)
{
    return (size + 3) & ~3;
}

static block_header_t *find_free_block(size_t size)
{
    uint8_t *current = (uint8_t *)KERNEL_HEAP_START;

    while (current < heap_ptr)
    {
        block_header_t *header = (block_header_t *)current;

        if (header->free && header->size >= size)
            return header;

        current += sizeof(block_header_t);
        current += header->size;
    }

    return NULL;
}

static void split_block(block_header_t *header, size_t size)
{
    size_t remaining = header->size - size;

    if (remaining <= sizeof(block_header_t) + 4)
        return;

    block_header_t *new_block =
        (block_header_t *)((uint8_t *)(header + 1) + size);

    new_block->size = remaining - sizeof(block_header_t);
    new_block->free = 1;

    header->size = size;
}

static void coalesce_blocks(void)
{
    uint8_t *current = (uint8_t *)KERNEL_HEAP_START;

    while (current < heap_ptr)
    {
        block_header_t *header = (block_header_t *)current;

        uint8_t *next_addr =
            current + sizeof(block_header_t) + header->size;

        if (next_addr >= heap_ptr)
            break;

        block_header_t *next =
            (block_header_t *)next_addr;

        if (header->free && next->free)
        {
            header->size +=
                sizeof(block_header_t) + next->size;

            continue;
        }

        current = next_addr;
    }
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

    block_header_t *header = find_free_block(size);

    if (header != NULL)
    {
        split_block(header, size);
        header->free = 0;
        return (void *)(header + 1);
    }

    if (heap_ptr + sizeof(block_header_t) + size > heap_end)
        return NULL;

    header = (block_header_t *)heap_ptr;

    header->size = size;
    header->free = 0;

    heap_ptr += sizeof(block_header_t);
    void *ptr = heap_ptr;
    heap_ptr += size;

    return ptr;
}

void kfree(void *ptr)
{
    if (ptr == NULL)
        return;

    block_header_t *header =
        (block_header_t *)ptr - 1;

    header->free = 1;
    coalesce_blocks();
}