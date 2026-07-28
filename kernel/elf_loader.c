#include "include/elf_loader.h"
#include "include/elf.h"
#include "include/vfs.h"
#include "include/string.h"
#include "include/memory.h"

#define ELF_LOAD_BASE 0x00200000

int elf_load(const void *image)
{
    const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)image;

    /* ELF Magic */
    if (ehdr->e_ident[0] != ELFMAG0 ||
        ehdr->e_ident[1] != ELFMAG1 ||
        ehdr->e_ident[2] != ELFMAG2 ||
        ehdr->e_ident[3] != ELFMAG3)
    {
        return -1;
    }

    /* ELF32 */
    if (ehdr->e_ident[4] != ELFCLASS32)
        return -1;

    /* Little Endian */
    if (ehdr->e_ident[5] != ELFDATA2LSB)
        return -1;

    /* Current Version */
    if (ehdr->e_version != EV_CURRENT)
        return -1;

    /* i386 */
    if (ehdr->e_machine != EM_386)
        return -1;

    /* Executable */
    if (ehdr->e_type != ET_EXEC)
        return -1;

    const Elf32_Phdr *phdr =
        (const Elf32_Phdr *)
        ((const uint8_t *)image + ehdr->e_phoff);

    for (uint16_t i = 0; i < ehdr->e_phnum; i++)
    {
        const Elf32_Phdr *segment = &phdr[i];

        if (segment->p_type != PT_LOAD)
            continue;

        memcpy(
            (void *)segment->p_vaddr,
            (const uint8_t *)image + segment->p_offset,
            segment->p_filesz);

        if (segment->p_memsz > segment->p_filesz)
        {
            memset(
                (uint8_t *)segment->p_vaddr + segment->p_filesz,
                0,
                segment->p_memsz - segment->p_filesz);
        }
    }

    ((void (*)(void))ehdr->e_entry)();

    return 0;
}

int elf_exec(const char *path)
{
    int size = vfs_size(path);

    if (size <= 0)
        return -1;

    void *buffer = kmalloc(size);

    if (!buffer)
        return -1;

    if (vfs_read(path, buffer, size) != size)
    {
        kfree(buffer);
        return -1;
    }

    int ret = elf_load(buffer);

    kfree(buffer);

    return ret;
}