#ifndef _ROXY_H
#define _ROXY_H

#include "abi.h"    /* struct procinfo, meminfo, statfs, pciinfo, REBOOT_* */

/* RoxyOS system information calls (user/lib/syscall.asm). -1 = failure. */

/* Process table slot: 1 filled in, 0 if the slot is free, -1 past the last slot. */
int procinfo(int slot, struct procinfo *out);

int meminfo(struct meminfo *out);

/* Usage of the file system that holds path. */
int statfs(const char *path, struct statfs *out);

/* PCI function index (0, 1, ...): 0, or -1 past the last one. */
int pciinfo(int index, struct pciinfo *out);

/* REBOOT_RESTART or REBOOT_POWEROFF. Returns only on failure. */
int reboot(int how);

#endif
