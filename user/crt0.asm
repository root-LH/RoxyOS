[BITS 32]

; User program entry: the kernel irets here with ESP at the top of the
; user stack. Calls main() and passes its return value to sys_exit.

global _start
extern main
extern sys_exit

section .text.start

_start:
    xor ebp, ebp            ; end of frame chain for debuggers
    call main
    push eax
    call sys_exit

.hang:                      ; sys_exit never returns
    jmp .hang
