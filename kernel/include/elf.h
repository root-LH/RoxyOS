#ifndef ELF_H
#define ELF_H

#include "types.h"

/* e_ident[] */
#define EI_NIDENT 16

/* Magic */
#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

/* ELF Class */
#define ELFCLASS32 1

/* Data Encoding */
#define ELFDATA2LSB 1

/* File Type */
#define ET_EXEC 2

/* Machine */
#define EM_386 3

/* Version */
#define EV_CURRENT 1

/* Program Header Type */
#define PT_NULL 0
#define PT_LOAD 1

typedef struct
{
    uint8_t  e_ident[EI_NIDENT];

    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;

    uint32_t e_entry;

    uint32_t e_phoff;
    uint32_t e_shoff;

    uint32_t e_flags;

    uint16_t e_ehsize;

    uint16_t e_phentsize;
    uint16_t e_phnum;

    uint16_t e_shentsize;
    uint16_t e_shnum;

    uint16_t e_shstrndx;

} Elf32_Ehdr;

typedef struct
{
    uint32_t p_type;

    uint32_t p_offset;

    uint32_t p_vaddr;

    uint32_t p_paddr;

    uint32_t p_filesz;

    uint32_t p_memsz;

    uint32_t p_flags;

    uint32_t p_align;

} Elf32_Phdr;

#endif