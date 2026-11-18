#ifndef PAGING_H
#define PAGING_H

#include "types.h"

#define PAGE_PRESENT    0x1
#define PAGE_WRITABLE   0x2
#define PAGE_USER       0x4

#define KERNEL_VIRTUAL_BASE 0xC0000000
#define V2P(virt) ((uint32_t)(virt) - KERNEL_VIRTUAL_BASE)
#define P2V(phys) ((uint32_t)(phys) + KERNEL_VIRTUAL_BASE)

/* Physical memory [0, KERNEL_PHYS_MAP_SIZE) is mapped at KERNEL_VIRTUAL_BASE. */
#define KERNEL_PHYS_MAP_SIZE (128 * 1024 * 1024)

#define KERNEL_PDE_START  (KERNEL_VIRTUAL_BASE >> 22)   /* 768 */

/*
 * User address space layout (per process):
 *   [USER_SPACE_START, USER_STACK_BOTTOM)  ELF segments
 *   [USER_STACK_BOTTOM, USER_SPACE_END)    user stack
 * 0~4MB stays a kernel-only identity mapping for now (heap, E820, GDT).
 */
#define USER_SPACE_START   0x00400000
#define USER_SPACE_END     KERNEL_VIRTUAL_BASE
#define USER_STACK_PAGES   4
#define USER_STACK_TOP     USER_SPACE_END
#define USER_STACK_BOTTOM  (USER_STACK_TOP - USER_STACK_PAGES * 4096)

void paging_init(void);

/* Maps a page in the kernel page directory (shared by every process). */
void map_page(uint32_t virt_addr, uint32_t phys_addr, uint32_t flags);

/* Page directories are referenced by their kernel virtual address. */
uint32_t *vmm_kernel_directory(void);
uint32_t *vmm_current_directory(void);

/* New directory: kernel half shared with the kernel directory, user half empty. */
uint32_t *vmm_create_address_space(void);

/* Frees every user page, user page table and the directory itself. */
void vmm_destroy_address_space(uint32_t *pd);

/* Maps virt -> phys in pd. Returns 0 on success, -1 if out of memory. */
int vmm_map(uint32_t *pd, uint32_t virt, uint32_t phys, uint32_t flags);

/* Returns the PTE for virt (0 if no page table / not present). */
uint32_t vmm_get_pte(uint32_t *pd, uint32_t virt);

/* Loads pd into CR3 if it is not already active. */
void vmm_switch(uint32_t *pd);

/*
 * Checks that [addr, addr+len) lies in user space and every page is mapped
 * user-accessible (and writable when write != 0). Returns 1 if OK.
 */
int vmm_user_range_ok(uint32_t *pd, uint32_t addr, uint32_t len, int write);

/* Returns the length of a NUL-terminated user string (< max), or -1 if invalid. */
int vmm_user_strlen(uint32_t *pd, const char *str, uint32_t max);

#endif
