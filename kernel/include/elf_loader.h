#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include "types.h"

int elf_load(const void *image);
int elf_exec(const char *path);

#endif