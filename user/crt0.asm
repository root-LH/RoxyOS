[BITS 32]

; User program entry: the kernel irets here with ESP at the top of the
; user stack. Calls main() and passes its return value to _exit.

global _start
extern main
extern _exit

section .text.start

_start:
    xor ebp, ebp            ; end of frame chain for debuggers
    call main
    push eax
    call _exit

.hang:                      ; sys_exit never returns
    jmp .hang
