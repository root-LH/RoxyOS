[BITS 32]

; int 0x80 wrappers (cdecl). eax = syscall number (kernel/include/uapi/abi.h),
; ebx/ecx/edx = arguments, result in eax. ebx is callee-saved in the i386
; ABI, so it is saved around the trap (the arguments are then at [esp+8]).

%macro SYSCALL 3            ; name, number, argument count
global %1
%1:
    push ebx
    mov eax, %2
%if %3 >= 1
    mov ebx, [esp+8]
%endif
%if %3 >= 2
    mov ecx, [esp+12]
%endif
%if %3 >= 3
    mov edx, [esp+16]
%endif
    int 0x80
    pop ebx
    ret
%endmacro

section .text

SYSCALL write,      0, 3    ; fd, buf, len
SYSCALL getkey,     1, 0    ; raw key, no echo
SYSCALL open,       2, 2    ; path, flags
SYSCALL read,       3, 3    ; fd, buf, len
SYSCALL close,      4, 1    ; fd
SYSCALL _exit,      5, 1    ; code (does not return)
SYSCALL sys_sleep,  6, 1    ; ticks
SYSCALL fork,       7, 0
SYSCALL exec,       8, 2    ; path, argv
SYSCALL waitpid,    9, 3    ; pid, &status, options
SYSCALL getpid,    10, 0
SYSCALL sbrk,      11, 1    ; increment
SYSCALL lseek,     12, 3    ; fd, offset, whence
SYSCALL stat,      13, 2    ; path, &st
SYSCALL kill,      14, 1    ; pid
SYSCALL yield,     15, 0
SYSCALL uptime_ms, 16, 0
