#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include "types.h"
#include "process.h"

/*
 * Loads an ELF executable into a new address space and starts it as a
 * ring 3 child of the caller, with argv on its stack and stdin/stdout/
 * stderr on the console. Returns the process, or 0 on failure.
 */
process_t *elf_spawn(const char *path, int argc, char *const argv[]);

/*
 * exec(): replaces the calling user process's image with path. On success
 * frame (the caller's int 0x80 frame) is rewritten to enter the new
 * program and 0 is returned; on failure the old image is untouched and -1
 * is returned. Descriptors stay open.
 */
int elf_exec(registers_t *frame, const char *path, int argc, char *const argv[]);

#endif
