#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include "types.h"
#include "process.h"

/*
 * Loads an ELF executable into a new address space and starts it as a
 * ring 3 child of the caller. Returns the process, or 0 on failure.
 */
process_t *elf_spawn(const char *path);

/* elf_spawn() + wait. Returns the exit code, or -1 if it could not start. */
int elf_exec(const char *path);

#endif
