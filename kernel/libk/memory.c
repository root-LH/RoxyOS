#include "../include/memory.h"

#define MAX_FRAMES 32768

static uint32_t pmm_bitmap[MAX_FRAMES / 32];
static uint32_t total_frames = 0;

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

static void bitmap_set(uint32_t frame) {
    pmm_bitmap[frame / 32] |= (1 << (frame % 32));
}

static void bitmap_clear(uint32_t frame) {
    pmm_bitmap[frame / 32] &= ~(1 << (frame % 32));
}

static int bitmap_test(uint32_t frame) {
    return (pmm_bitmap[frame / 32] & (1 << (frame % 32))) != 0;
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

void pmm_init(uint32_t total_memory_mb) {
    uint16_t entry_count = *((uint16_t*)0x9000);
    struct e820_entry* mmap = (struct e820_entry*)0x9004;

    total_frames = (total_memory_mb * 1024 * 1024) / PAGE_SIZE;

    for (uint32_t i = 0; i < total_frames / 32; i++) {
        pmm_bitmap[i] = 0xFFFFFFFF;
    }

    for (int i = 0; i < entry_count; i++) {
        if (mmap[i].type == 1) {
            uint32_t start_frame = (uint32_t)((mmap[i].base_addr + PAGE_SIZE - 1) / PAGE_SIZE);
            uint32_t frame_count = (uint32_t)(mmap[i].length / PAGE_SIZE);

            for (uint32_t f = 0; f < frame_count; f++) {
                if (start_frame + f < total_frames) {
                    bitmap_clear(start_frame + f);
                }
            }
        }
    }

    uint32_t protect_end_addr = KERNEL_HEAP_START + KERNEL_HEAP_SIZE;
    uint32_t protect_frames = (protect_end_addr + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint32_t f = 0; f < protect_frames; f++)
    {
        bitmap_set(f);
    }
}

void* pmm_alloc_frame(void) {
    for (uint32_t f = 0; f < total_frames; f++) {
        if (!bitmap_test(f)) {
            bitmap_set(f);
            return (void*)(f * PAGE_SIZE);
        }
    }
    return 0;
}

void pmm_free_frame(void* frame_addr) {
    uint32_t frame = (uint32_t)frame_addr / PAGE_SIZE;
    if (frame < total_frames) {
        bitmap_clear(frame);
    }
}
uint32_t pmm_total_frame_count(void)
{
    return total_frames;
}

uint32_t pmm_free_frame_count(void)
{
    uint32_t count = 0;

    for (uint32_t f = 0; f < total_frames; f++)
    {
        if (!bitmap_test(f))
            count++;
    }

    return count;
}
