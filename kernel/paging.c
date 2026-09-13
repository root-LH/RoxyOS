#include "include/paging.h"
#include "include/memory.h"
#include "include/printk.h"

#define KERNEL_VIRTUAL_BASE 0xC0000000
#define V2P(virt) ((uint32_t)(virt) - KERNEL_VIRTUAL_BASE)
#define P2V(phys) ((uint32_t)(phys) + KERNEL_VIRTUAL_BASE)

static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t first_page_table[1024] __attribute__((aligned(4096)));

extern uint32_t page_directory[1024];

extern void load_page_directory(uint32_t* page_directory);
extern void enable_paging(void);

void paging_init(void)
{
    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0x00000002;
        first_page_table[i] = 0x00000002;
    }

    for (uint32_t i = 0; i < 1024; i++) {
        uint32_t phy_addr = i * PAGE_SIZE;
        first_page_table[i] = phy_addr | PAGE_PRESENT | PAGE_WRITABLE;
    }

    page_directory[0] = V2P(first_page_table) | PAGE_PRESENT | PAGE_WRITABLE;
    page_directory[768] = V2P(first_page_table) | PAGE_PRESENT | PAGE_WRITABLE;

    load_page_directory((uint32_t*)V2P(page_directory));
    enable_paging();

    printk("Paging enabled successfully!\n");
}

void map_page(uint32_t virt_addr, uint32_t phys_addr, uint32_t flags)
{
    uint32_t pd_index = virt_addr >> 22;
    uint32_t pt_index = (virt_addr >> 12) & 0x3FF;

    uint32_t *page_table;

    if (!(page_directory[pd_index] & PAGE_PRESENT)) {
        uint32_t new_table_phys = (uint32_t)pmm_alloc_frame();

        page_table = (uint32_t*)P2V(new_table_phys);
        for (int i = 0; i < 1024; i++) {
            page_table[i] = 0x00000002;
        }

        page_directory[pd_index] = new_table_phys | PAGE_PRESENT | PAGE_WRITABLE | flags;
    } else {
        uint32_t pt_phys = page_directory[pd_index] & ~0xFFF;
        page_table = (uint32_t*)P2V(pt_phys);
    }

    page_table[pt_index] = (phys_addr & ~0xFFF) | PAGE_PRESENT | flags;

    __asm__ __volatile__("invlpg (%0)" :: "r" (virt_addr) : "memory");
}