#include "../include/memory.h"
#include "../include/paging.h"
#include "../include/panic.h"
#include "../include/printk.h"
#include "../include/cpu.h"

/* ------------------------------------------------------------------ */
/* Physical Memory Manager                                             */
/* ------------------------------------------------------------------ */

/* The PMM only hands out frames that P2V() can reach. */
#define MAX_FRAMES (KERNEL_PHYS_MAP_SIZE / PAGE_SIZE)

/* Low memory (BIOS data, boot code, E820, kernel image, VGA, EBDA). */
#define LOW_RESERVED_END 0x100000

extern uint8_t _kernel_end[];

static uint32_t pmm_bitmap[MAX_FRAMES / 32];
static uint32_t total_frames = 0;

static struct e820_entry e820_map[E820_MAX_ENTRIES];
static uint32_t e820_count = 0;

static void bitmap_set(uint32_t frame) {
    pmm_bitmap[frame / 32] |= (1u << (frame % 32));
}

static void bitmap_clear(uint32_t frame) {
    pmm_bitmap[frame / 32] &= ~(1u << (frame % 32));
}

static int bitmap_test(uint32_t frame) {
    return (pmm_bitmap[frame / 32] & (1u << (frame % 32))) != 0;
}

void pmm_init(void)
{
    /*
     * Stage2 left the E820 map at physical 0x9000. Copy it into the kernel
     * through the higher-half mapping so nothing depends on low addresses.
     */
    uint16_t count = *(volatile uint16_t *)P2V(E820_COUNT_ADDR);
    const struct e820_entry *src = (const struct e820_entry *)P2V(E820_MAP_ADDR);

    if (count == 0)
        panic("pmm: BIOS returned no E820 memory map");

    if (count > E820_MAX_ENTRIES)
        count = E820_MAX_ENTRIES;

    for (uint32_t i = 0; i < count; i++)
        e820_map[i] = src[i];

    e820_count = count;

    for (uint32_t i = 0; i < MAX_FRAMES / 32; i++)
        pmm_bitmap[i] = 0xFFFFFFFF;

    /* Free every whole frame inside a usable region, up to the P2V limit. */
    uint32_t top_frame = 0;

    for (uint32_t i = 0; i < e820_count; i++)
    {
        if (e820_map[i].type != E820_USABLE)
            continue;

        uint64_t base = e820_map[i].base_addr;
        uint64_t end  = base + e820_map[i].length;

        if (end > KERNEL_PHYS_MAP_SIZE)
            end = KERNEL_PHYS_MAP_SIZE;

        if (base >= end)
            continue;

        uint32_t first = (uint32_t)((base + PAGE_SIZE - 1) >> 12);
        uint32_t last  = (uint32_t)(end >> 12);     /* exclusive */

        for (uint32_t f = first; f < last; f++)
            bitmap_clear(f);

        if (last > top_frame)
            top_frame = last;
    }

    total_frames = top_frame;

    /* Keep low memory and the kernel image (incl. BSS) reserved. */
    uint32_t reserved_end = V2P(_kernel_end);

    if (reserved_end < LOW_RESERVED_END)
        reserved_end = LOW_RESERVED_END;

    for (uint32_t f = 0; f < (reserved_end + PAGE_SIZE - 1) / PAGE_SIZE; f++)
        bitmap_set(f);

    klog("[pmm] %u E820 entries, %u MB managed, %u KB free\n",
         e820_count, total_frames / 256, pmm_free_frame_count() * 4);
}

static void *pmm_alloc_frame_unlocked(void) {
    for (uint32_t f = 0; f < total_frames; f++) {
        if (!bitmap_test(f)) {
            bitmap_set(f);
            return (void*)(f * PAGE_SIZE);
        }
    }
    return 0;
}

static void pmm_free_frame_unlocked(void* frame_addr) {
    uint32_t frame = (uint32_t)frame_addr / PAGE_SIZE;
    if (frame < total_frames) {
        bitmap_clear(frame);
    }
}

/*
 * The PMM and the heap are used from preemptible kernel threads, from
 * system calls and from the IRQ path (process reaping), so every entry
 * point runs with interrupts disabled.
 */
void *pmm_alloc_frame(void)
{
    uint32_t flags = irq_save();
    void *frame = pmm_alloc_frame_unlocked();
    irq_restore(flags);

    return frame;
}

void pmm_free_frame(void *frame_addr)
{
    uint32_t flags = irq_save();
    pmm_free_frame_unlocked(frame_addr);
    irq_restore(flags);
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

const struct e820_entry *pmm_memory_map(uint32_t *count)
{
    *count = e820_count;
    return e820_map;
}

/* ------------------------------------------------------------------ */
/* Kernel heap                                                         */
/* ------------------------------------------------------------------ */

/*
 * The heap lives at KERNEL_HEAP_START in the kernel half, so every address
 * space sees it. heap_ptr is the end of the last block; heap_brk is the end
 * of the mapped area. kbrk() maps more frames when a block does not fit.
 */
static uint8_t *const heap_start = (uint8_t *)KERNEL_HEAP_START;
static uint8_t *heap_ptr = (uint8_t *)KERNEL_HEAP_START;
static uint8_t *heap_brk = (uint8_t *)KERNEL_HEAP_START;
static int heap_ready = 0;

static size_t align4(size_t size)
{
    return (size + 3) & ~3;
}

void heap_init(void)
{
    heap_ptr = heap_start;
    heap_brk = heap_start;
    heap_ready = 1;

    if (!kbrk(KERNEL_HEAP_INITIAL))
        panic("heap_init: cannot map initial heap");

    klog("[heap] %p, %u KB mapped, max %u MB\n", heap_start,
         KERNEL_HEAP_INITIAL / 1024, KERNEL_HEAP_MAX / (1024 * 1024));
}

void *kbrk(size_t increment)
{
    if (!heap_ready)
        panic("kbrk: heap used before heap_init");

    uint8_t *old = heap_brk;
    uint32_t size = (increment + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    if (size == 0)
        return old;

    if (size > KERNEL_HEAP_MAX - (uint32_t)(heap_brk - heap_start))
        return NULL;

    for (uint32_t off = 0; off < size; off += PAGE_SIZE)
    {
        uint32_t frame = (uint32_t)pmm_alloc_frame();

        if (!frame)
        {
            /* Roll back the pages mapped by this call. */
            while (off > 0)
            {
                off -= PAGE_SIZE;
                pmm_free_frame((void *)unmap_page((uint32_t)old + off));
            }
            return NULL;
        }

        map_page((uint32_t)old + off, frame, PAGE_WRITABLE);
    }

    heap_brk = old + size;
    return old;
}

static block_header_t *find_free_block(size_t size)
{
    uint8_t *current = heap_start;

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
    uint8_t *current = heap_start;

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

static void *kmalloc_unlocked(size_t size)
{
    if (size == 0 || size > KERNEL_HEAP_MAX)
        return NULL;

    size = align4(size);

    block_header_t *header = find_free_block(size);

    if (header != NULL)
    {
        split_block(header, size);
        header->free = 0;
        return (void *)(header + 1);
    }

    uint32_t need = sizeof(block_header_t) + size;

    if ((uint32_t)(heap_brk - heap_ptr) < need)
    {
        if (!kbrk(need - (uint32_t)(heap_brk - heap_ptr)))
            return NULL;
    }

    header = (block_header_t *)heap_ptr;

    header->size = size;
    header->free = 0;

    heap_ptr += need;

    return (void *)(header + 1);
}

static void kfree_unlocked(void *ptr)
{
    if (ptr == NULL)
        return;

    if ((uint8_t *)ptr < heap_start + sizeof(block_header_t) ||
        (uint8_t *)ptr >= heap_ptr)
    {
        klog("[heap] kfree: %p is not a heap pointer\n", ptr);
        return;
    }

    block_header_t *header =
        (block_header_t *)ptr - 1;

    header->free = 1;
    coalesce_blocks();
}

void *kmalloc(size_t size)
{
    uint32_t flags = irq_save();
    void *ptr = kmalloc_unlocked(size);
    irq_restore(flags);

    return ptr;
}

void kfree(void *ptr)
{
    uint32_t flags = irq_save();
    kfree_unlocked(ptr);
    irq_restore(flags);
}

void heap_get_stats(heap_stats_t *stats)
{
    stats->mapped = (uint32_t)(heap_brk - heap_start);
    stats->used = 0;
    stats->free = 0;
    stats->blocks = 0;

    for (uint8_t *cur = heap_start; cur < heap_ptr; )
    {
        block_header_t *header = (block_header_t *)cur;

        if (header->free)
            stats->free += header->size;
        else
            stats->used += header->size;

        stats->blocks++;
        cur += sizeof(block_header_t) + header->size;
    }

    /* Mapped but never handed out yet. */
    stats->free += (uint32_t)(heap_brk - heap_ptr);
}
