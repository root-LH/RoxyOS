#ifndef SYSCALL_H
#define SYSCALL_H

#include "isr.h"
#include "uapi/abi.h"   /* SYS_* numbers and the flags user programs pass */

/* int 0x80 entry (isr128). The result is returned in r->eax. */
void syscall_handler(registers_t *r);

#endif
