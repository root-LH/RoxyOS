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
 *   [USER_SPACE_START, ...)                ELF segments, then the sbrk heap
 *   [USER_STACK_BOTTOM, USER_SPACE_END)    user stack
 * 0~4MB is left unmapped so NULL pointer accesses fault.
 * Kernel half: 0xC0000000~ physical map, KERNEL_HEAP_START (memory.h) heap.
 */
#define USER_SPACE_START   0x00400000
#define USER_SPACE_END     KERNEL_VIRTUAL_BASE
#define USER_STACK_PAGES   4
#define USER_STACK_TOP     USER_SPACE_END
#define USER_STACK_BOTTOM  (USER_STACK_TOP - USER_STACK_PAGES * 4096)

/* The heap (sbrk) starts after the ELF image and stops a guard page below the stack. */
#define USER_HEAP_LIMIT    (USER_STACK_BOTTOM - 4096)

#define PAGE_ALIGN_UP(x)   (((uint32_t)(x) + 4095) & ~4095u)

void paging_init(void);

/*
 * Kernel-half helpers (shared by every process). map_page panics on failure.
 * unmap_page returns the frame that was mapped (0 if none); it does not free it.
 */
void map_page(uint32_t virt_addr, uint32_t phys_addr, uint32_t flags);
uint32_t unmap_page(uint32_t virt_addr);

/* Translates virt in the active directory. Returns 0 and sets *phys, or -1. */
int get_physaddr(uint32_t virt_addr, uint32_t *phys);

/* Page directories are referenced by their kernel virtual address. */
uint32_t *vmm_kernel_directory(void);
uint32_t *vmm_current_directory(void);

/* New directory: kernel half shared with the kernel directory, user half empty. */
uint32_t *vmm_create_address_space(void);

/* New directory whose user half is a private copy of src's (for fork). */
uint32_t *vmm_clone_address_space(uint32_t *src);

/* Frees every user page, user page table and the directory itself. */
void vmm_destroy_address_space(uint32_t *pd);

/* Maps virt -> phys in pd. Returns 0 on success, -1 if out of memory. */
int vmm_map(uint32_t *pd, uint32_t virt, uint32_t phys, uint32_t flags);

/*
 * Removes the mapping for virt and returns the frame it pointed to (0 if it
 * was not mapped). The frame is not freed. An empty user page table is freed.
 */
uint32_t vmm_unmap(uint32_t *pd, uint32_t virt);

/* Translates virt in pd. Returns 0 and sets *phys (with page offset), or -1. */
int vmm_get_physaddr(uint32_t *pd, uint32_t virt, uint32_t *phys);

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

/*
 * Copy between the kernel and the user half of pd (which need not be the
 * active directory). Every user page must be mapped user-accessible, and
 * writable for copy_to_user. Return 0, or -1 if any page is not.
 */
int copy_from_user(uint32_t *pd, void *dst, uint32_t src, uint32_t len);
int copy_to_user(uint32_t *pd, uint32_t dst, const void *src, uint32_t len);

/*
 * Copies a NUL-terminated user string into dst (size bytes). Returns its
 * length, or -1 if a page is not accessible or no NUL fits in size.
 */
int strncpy_from_user(uint32_t *pd, char *dst, uint32_t src, uint32_t size);

#endif
