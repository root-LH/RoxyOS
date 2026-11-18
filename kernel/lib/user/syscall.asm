[BITS 32]

; int 0x80 wrappers (cdecl). eax = syscall number, ebx/ecx/edx = args,
; return value in eax. ebx is callee-saved in the i386 ABI, so every
; wrapper that uses it saves and restores it (args are then at [esp+8]).

global sys_write
global sys_getchar
global sys_open
global sys_read
global sys_close
global sys_exit
global sys_sleep

%macro SYSCALL1 2           ; name, number
%1:
    push ebx
    mov eax, %2
    mov ebx, [esp+8]
    int 0x80
    pop ebx
    ret
%endmacro

SYSCALL1 sys_write, 0       ; str
SYSCALL1 sys_open,  2       ; path
SYSCALL1 sys_close, 4       ; fd
SYSCALL1 sys_exit,  5       ; exit code (does not return)
SYSCALL1 sys_sleep, 6       ; ticks

sys_getchar:
    mov eax, 1
    int 0x80
    ret

sys_read:
    push ebx
    mov eax, 3
    mov ebx, [esp+8]        ; fd
    mov ecx, [esp+12]       ; buffer
    mov edx, [esp+16]       ; size
    int 0x80
    pop ebx
    ret
