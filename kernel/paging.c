#include "include/paging.h"
#include "include/memory.h"
#include "include/string.h"
#include "include/printk.h"
#include "include/panic.h"

#define PDE_COUNT 1024
#define PTE_COUNT 1024

#define PDE_INDEX(v) ((uint32_t)(v) >> 22)
#define PTE_INDEX(v) (((uint32_t)(v) >> 12) & 0x3FF)
#define ENTRY_ADDR(e) ((e) & ~0xFFFu)

static uint32_t kernel_directory[PDE_COUNT] __attribute__((aligned(4096)));
/* Maps physical 0~4MB at KERNEL_VIRTUAL_BASE (the first higher-half table). */
static uint32_t low_table[PTE_COUNT] __attribute__((aligned(4096)));

static uint32_t *current_directory = kernel_directory;

extern void load_page_directory(uint32_t *page_directory);
extern void enable_paging(void);

static uint32_t *alloc_zeroed_frame(uint32_t *phys_out)
{
    uint32_t phys = (uint32_t)pmm_alloc_frame();

    if (!phys)
        return 0;

    if (phys >= KERNEL_PHYS_MAP_SIZE)
        panic("vmm: frame %p outside kernel physical map", (void *)phys);

    uint32_t *virt = (uint32_t *)P2V(phys);
    memset(virt, 0, PAGE_SIZE);

    *phys_out = phys;
    return virt;
}

static void load_cr3(uint32_t *pd)
{
    asm volatile("mov %0, %%cr3" : : "r"(V2P(pd)) : "memory");
}

static void invlpg(uint32_t virt)
{
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

/*
 * A TLB entry must be flushed when pd is active, or when virt is in the
 * kernel half: those page tables are shared, so the change is live in
 * whatever directory is loaded right now.
 */
static void flush_if_live(uint32_t *pd, uint32_t virt)
{
    if (pd == current_directory || PDE_INDEX(virt) >= KERNEL_PDE_START)
        invlpg(virt);
}

void paging_init(void)
{
    for (uint32_t i = 0; i < PTE_COUNT; i++)
        low_table[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITABLE;

    /*
     * No identity mapping: the user half (PDE 0~767) starts empty, so a
     * NULL or low-address access faults. Everything the kernel needs from
     * low memory (E820, VGA, GDT) goes through P2V() or was copied.
     */
    for (uint32_t i = 0; i < PDE_COUNT; i++)
        kernel_directory[i] = 0;

    /*
     * Higher half: map physical [0, KERNEL_PHYS_MAP_SIZE) at 3GB so P2V()
     * works for every frame the PMM hands out. The first table maps
     * 0~4MB. The rest are allocated from the PMM; while we are still
     * on the boot page tables only 0~4MB is reachable, which is where the
     * PMM's first free frames are (just above 1MB / the kernel image).
     */
    kernel_directory[KERNEL_PDE_START] =
        V2P(low_table) | PAGE_PRESENT | PAGE_WRITABLE;

    uint32_t phys_tables = KERNEL_PHYS_MAP_SIZE / (4 * 1024 * 1024);

    for (uint32_t t = 1; t < phys_tables; t++)
    {
        uint32_t table_phys;
        uint32_t *table = alloc_zeroed_frame(&table_phys);

        if (!table || table_phys >= 4 * 1024 * 1024)
            panic("paging_init: cannot allocate early page table");

        for (uint32_t i = 0; i < PTE_COUNT; i++)
        {
            uint32_t phys = t * 4 * 1024 * 1024 + i * PAGE_SIZE;
            table[i] = phys | PAGE_PRESENT | PAGE_WRITABLE;
        }

        kernel_directory[KERNEL_PDE_START + t] =
            table_phys | PAGE_PRESENT | PAGE_WRITABLE;
    }

    load_page_directory((uint32_t *)V2P(kernel_directory));
    enable_paging();

    /*
     * Pre-create every remaining kernel page table so the kernel half of
     * all directories can be shared by copying PDEs once: a later kernel
     * mapping lands in an existing table and is seen by every process.
     */
    for (uint32_t i = KERNEL_PDE_START + phys_tables; i < PDE_COUNT; i++)
    {
        uint32_t table_phys;

        if (!alloc_zeroed_frame(&table_phys))
            panic("paging_init: out of memory for kernel page tables");

        kernel_directory[i] = table_phys | PAGE_PRESENT | PAGE_WRITABLE;
    }

    current_directory = kernel_directory;

    printk("Paging enabled successfully!\n");
    klog("[vmm] kernel maps %u MB physical at %p\n",
         KERNEL_PHYS_MAP_SIZE / (1024 * 1024), (void *)KERNEL_VIRTUAL_BASE);
}

uint32_t *vmm_kernel_directory(void)
{
    return kernel_directory;
}

uint32_t *vmm_current_directory(void)
{
    return current_directory;
}

int vmm_map(uint32_t *pd, uint32_t virt, uint32_t phys, uint32_t flags)
{
    uint32_t pdi = PDE_INDEX(virt);
    uint32_t *table;

    if (!(pd[pdi] & PAGE_PRESENT))
    {
        if (pdi >= KERNEL_PDE_START)
            panic("vmm_map: missing kernel page table for %p", (void *)virt);

        uint32_t table_phys;
        table = alloc_zeroed_frame(&table_phys);

        if (!table)
            return -1;

        /* PDE gets the widest rights; the PTE decides per page. */
        pd[pdi] = table_phys | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    }
    else
    {
        table = (uint32_t *)P2V(ENTRY_ADDR(pd[pdi]));

        if (flags & PAGE_USER)
            pd[pdi] |= PAGE_USER | PAGE_WRITABLE;
    }

    table[PTE_INDEX(virt)] = ENTRY_ADDR(phys) | PAGE_PRESENT | flags;

    flush_if_live(pd, virt);

    return 0;
}

uint32_t vmm_unmap(uint32_t *pd, uint32_t virt)
{
    uint32_t pdi = PDE_INDEX(virt);

    if (!(pd[pdi] & PAGE_PRESENT))
        return 0;

    uint32_t *table = (uint32_t *)P2V(ENTRY_ADDR(pd[pdi]));
    uint32_t pte = table[PTE_INDEX(virt)];

    if (!(pte & PAGE_PRESENT))
        return 0;

    table[PTE_INDEX(virt)] = 0;
    flush_if_live(pd, virt);

    /* Kernel tables are shared by every directory and never freed. */
    if (pdi >= KERNEL_PDE_START)
        return ENTRY_ADDR(pte);

    for (uint32_t i = 0; i < PTE_COUNT; i++)
    {
        if (table[i] & PAGE_PRESENT)
            return ENTRY_ADDR(pte);
    }

    /* Last page of this user table: release the table itself. */
    pd[pdi] = 0;
    pmm_free_frame((void *)V2P(table));

    if (pd == current_directory)
        load_cr3(pd);   /* drop any cached PDE */

    return ENTRY_ADDR(pte);
}

int vmm_get_physaddr(uint32_t *pd, uint32_t virt, uint32_t *phys)
{
    uint32_t pte = vmm_get_pte(pd, virt);

    if (!pte)
        return -1;

    *phys = ENTRY_ADDR(pte) | (virt & 0xFFFu);
    return 0;
}

void map_page(uint32_t virt_addr, uint32_t phys_addr, uint32_t flags)
{
    if (PDE_INDEX(virt_addr) < KERNEL_PDE_START)
        panic("map_page: %p is not a kernel address", (void *)virt_addr);

    if (vmm_map(kernel_directory, virt_addr, phys_addr, flags) != 0)
        panic("map_page: out of memory");
}

uint32_t unmap_page(uint32_t virt_addr)
{
    if (PDE_INDEX(virt_addr) < KERNEL_PDE_START)
        panic("unmap_page: %p is not a kernel address", (void *)virt_addr);

    return vmm_unmap(kernel_directory, virt_addr);
}

int get_physaddr(uint32_t virt_addr, uint32_t *phys)
{
    return vmm_get_physaddr(current_directory, virt_addr, phys);
}

uint32_t vmm_get_pte(uint32_t *pd, uint32_t virt)
{
    uint32_t pde = pd[PDE_INDEX(virt)];

    if (!(pde & PAGE_PRESENT))
        return 0;

    uint32_t *table = (uint32_t *)P2V(ENTRY_ADDR(pde));
    uint32_t pte = table[PTE_INDEX(virt)];

    return (pte & PAGE_PRESENT) ? pte : 0;
}

uint32_t *vmm_create_address_space(void)
{
    uint32_t phys;
    uint32_t *pd = alloc_zeroed_frame(&phys);

    if (!pd)
        return 0;

    /* The whole kernel half is shared; the user half starts empty. */
    for (uint32_t i = KERNEL_PDE_START; i < PDE_COUNT; i++)
        pd[i] = kernel_directory[i];

    return pd;
}

void vmm_destroy_address_space(uint32_t *pd)
{
    if (!pd || pd == kernel_directory)
        return;

    if (pd == current_directory)
        panic("vmm_destroy_address_space: directory is active");

    for (uint32_t i = 0; i < KERNEL_PDE_START; i++)
    {
        if (!(pd[i] & PAGE_PRESENT))
            continue;

        uint32_t *table = (uint32_t *)P2V(ENTRY_ADDR(pd[i]));

        for (uint32_t j = 0; j < PTE_COUNT; j++)
        {
            if (table[j] & PAGE_PRESENT)
                pmm_free_frame((void *)ENTRY_ADDR(table[j]));
        }

        pmm_free_frame((void *)ENTRY_ADDR(pd[i]));
    }

    pmm_free_frame((void *)V2P(pd));
}

void vmm_switch(uint32_t *pd)
{
    if (!pd)
        pd = kernel_directory;

    if (pd == current_directory)
        return;

    current_directory = pd;
    load_cr3(pd);
}

int vmm_user_range_ok(uint32_t *pd, uint32_t addr, uint32_t len, int write)
{
    if (len == 0)
        return 1;

    uint32_t end = addr + len;

    if (end < addr || addr < USER_SPACE_START || end > USER_SPACE_END)
        return 0;

    uint32_t need = PAGE_PRESENT | PAGE_USER | (write ? PAGE_WRITABLE : 0);

    for (uint32_t page = addr & ~0xFFFu; page < end; page += PAGE_SIZE)
    {
        if ((vmm_get_pte(pd, page) & need) != need)
            return 0;
    }

    return 1;
}

int vmm_user_strlen(uint32_t *pd, const char *str, uint32_t max)
{
    uint32_t addr = (uint32_t)str;
    uint32_t checked_page = 1;  /* never a page address */

    for (uint32_t i = 0; i < max; i++, addr++)
    {
        uint32_t page = addr & ~0xFFFu;

        if (page != checked_page)
        {
            if (!vmm_user_range_ok(pd, addr, 1, 0))
                return -1;

            checked_page = page;
        }

        if (*(const char *)addr == '\0')
            return (int)i;
    }

    return -1;
}
