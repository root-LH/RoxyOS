#include "include/paging.h"
#include "include/memory.h"
#include "include/printk.h"

static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t first_page_table[1024] __attribute__((aligned(4096)));

extern void load_page_directory(uint32_t* page_directory);
extern void enable_paging(void);

void paging_init(void) {
    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0x00000002;
        first_page_table[i] = 0x00000002;
    }

    for (uint32_t i = 0; i < 1024; i++) {
        uint32_t phy_addr = i * PAGE_SIZE;
        first_page_table[i] = phy_addr | PAGE_PRESENT | PAGE_WRITABLE;
    }

    page_directory[0] = ((uint32_t)first_page_table) | PAGE_PRESENT | PAGE_WRITABLE;

    load_page_directory(page_directory);
    enable_paging();

    printk("Paging enabled successfully!\n");
}