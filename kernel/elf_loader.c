#include "include/elf_loader.h"
#include "include/elf.h"
#include "include/vfs.h"
#include "include/string.h"
#include "include/memory.h"
#include "include/paging.h"
#include "include/process.h"
#include "include/scheduler.h"
#include "include/printk.h"
#include "include/file.h"

#ifndef PF_W
#define PF_W 0x2
#endif

#define PAGE_MASK (~(PAGE_SIZE - 1))

static int elf_check_header(const Elf32_Ehdr *ehdr, uint32_t size)
{
    if (size < sizeof(Elf32_Ehdr))
        return -1;

    if (ehdr->e_ident[0] != ELFMAG0 ||
        ehdr->e_ident[1] != ELFMAG1 ||
        ehdr->e_ident[2] != ELFMAG2 ||
        ehdr->e_ident[3] != ELFMAG3)
        return -1;

    if (ehdr->e_ident[4] != ELFCLASS32 ||
        ehdr->e_ident[5] != ELFDATA2LSB ||
        ehdr->e_version != EV_CURRENT ||
        ehdr->e_machine != EM_386 ||
        ehdr->e_type != ET_EXEC)
        return -1;

    if (ehdr->e_phentsize != sizeof(Elf32_Phdr))
        return -1;

    uint32_t ph_end = ehdr->e_phoff + (uint32_t)ehdr->e_phnum * sizeof(Elf32_Phdr);

    if (ph_end < ehdr->e_phoff || ph_end > size)
        return -1;

    return 0;
}

static int user_range_valid(uint32_t start, uint32_t len, uint32_t limit)
{
    uint32_t end = start + len;

    return end >= start && start >= USER_SPACE_START && end <= limit;
}

/*
 * Makes sure the user page containing virt is mapped in pd and returns its
 * kernel-visible address. Pages shared by two segments are reused, and
 * their rights only ever widen.
 */
static uint8_t *ensure_user_page(uint32_t *pd, uint32_t virt, uint32_t flags)
{
    uint32_t page = virt & PAGE_MASK;
    uint32_t pte = vmm_get_pte(pd, page);

    if (pte)
    {
        if ((pte & flags) != flags)
            vmm_map(pd, page, pte & PAGE_MASK, (pte & 0xFFF) | flags);

        return (uint8_t *)P2V(pte & PAGE_MASK);
    }

    uint32_t frame = (uint32_t)pmm_alloc_frame();

    if (!frame)
        return 0;

    uint8_t *kva = (uint8_t *)P2V(frame);
    memset(kva, 0, PAGE_SIZE);

    if (vmm_map(pd, page, frame, flags) != 0)
    {
        pmm_free_frame((void *)frame);
        return 0;
    }

    return kva;
}

/* Copies (or zero-fills when src == 0) len bytes into the user address space. */
static int copy_to_user_space(uint32_t *pd, uint32_t dst, const uint8_t *src,
                              uint32_t len, uint32_t flags)
{
    while (len > 0)
    {
        uint8_t *page = ensure_user_page(pd, dst, flags);

        if (!page)
            return -1;

        uint32_t offset = dst & (PAGE_SIZE - 1);
        uint32_t chunk = PAGE_SIZE - offset;

        if (chunk > len)
            chunk = len;

        if (src)
        {
            memcpy(page + offset, src, chunk);
            src += chunk;
        }
        /* fresh frames are already zeroed; shared ones get explicit zeros */
        else
        {
            memset(page + offset, 0, chunk);
        }

        dst += chunk;
        len -= chunk;
    }

    return 0;
}

static int load_segments(uint32_t *pd, const uint8_t *image, uint32_t size)
{
    const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)image;
    const Elf32_Phdr *phdr = (const Elf32_Phdr *)(image + ehdr->e_phoff);
    int loaded = 0;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++)
    {
        const Elf32_Phdr *seg = &phdr[i];

        if (seg->p_type != PT_LOAD || seg->p_memsz == 0)
            continue;

        if (seg->p_filesz > seg->p_memsz)
            return -1;

        if (seg->p_offset + seg->p_filesz < seg->p_offset ||
            seg->p_offset + seg->p_filesz > size)
            return -1;

        /* Segments must stay in user space, below the stack. */
        if (!user_range_valid(seg->p_vaddr, seg->p_memsz, USER_STACK_BOTTOM))
        {
            kprintf("elf: segment %p+%x outside user space\n",
                    (void *)seg->p_vaddr, seg->p_memsz);
            return -1;
        }

        uint32_t flags = PAGE_PRESENT | PAGE_USER;

        if (seg->p_flags & PF_W)
            flags |= PAGE_WRITABLE;

        if (copy_to_user_space(pd, seg->p_vaddr,
                               image + seg->p_offset, seg->p_filesz, flags) != 0)
            return -1;

        if (copy_to_user_space(pd, seg->p_vaddr + seg->p_filesz, 0,
                               seg->p_memsz - seg->p_filesz, flags) != 0)
            return -1;

        loaded++;
    }

    return loaded > 0 ? 0 : -1;
}

static int map_user_stack(uint32_t *pd)
{
    for (uint32_t va = USER_STACK_BOTTOM; va < USER_STACK_TOP; va += PAGE_SIZE)
    {
        if (!ensure_user_page(pd, va, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER))
            return -1;
    }

    return 0;
}

process_t *elf_spawn(const char *path)
{
    int size = vfs_size(path);

    if (size <= 0)
        return 0;

    uint8_t *image = kmalloc(size);

    if (!image)
        return 0;

    process_t *proc = 0;
    uint32_t *pd = 0;

    if (vfs_read(path, image, size) != size)
        goto out;

    if (elf_check_header((const Elf32_Ehdr *)image, size) != 0)
    {
        kprintf("elf: %s is not an i386 ELF executable\n", path);
        goto out;
    }

    uint32_t entry = ((const Elf32_Ehdr *)image)->e_entry;

    if (!user_range_valid(entry, 1, USER_STACK_BOTTOM))
    {
        kprintf("elf: entry %p outside user space\n", (void *)entry);
        goto out;
    }

    pd = vmm_create_address_space();

    if (!pd)
        goto out;

    if (load_segments(pd, image, size) != 0 || map_user_stack(pd) != 0)
        goto out;

    proc = process_create_user(pd, entry, USER_STACK_TOP);

    if (!proc)
        goto out;

    proc->parent = process_current();
    pd = 0;     /* owned by the process now */

    if (fd_open_stdio(proc) != 0)
    {
        kprintf("elf: out of open files\n");
        proc->parent = 0;   /* nobody will wait: reap it right away */
        process_kill(proc, -1);
        proc = 0;
        goto out;
    }

    klog("[elf] %s -> pid %u, entry %p\n", path, proc->pid, (void *)entry);

    scheduler_add(proc);

out:
    if (pd)
        vmm_destroy_address_space(pd);

    kfree(image);

    return proc;
}

int elf_exec(const char *path)
{
    process_t *proc = elf_spawn(path);

    if (!proc)
        return -1;

    int code = process_wait(proc);

    klog("[elf] %s exited with %d\n", path, code);

    return code;
}
