[BITS 32]

; User program entry. The kernel irets here with ESP pointing at
;   [esp]     argc
;   [esp + 4] argv  (NULL-terminated array of strings, see elf_loader.c)
; Calls main(argc, argv) and passes its return value to exit().

global _start
extern main
extern exit

section .text.start

_start:
    xor ebp, ebp            ; end of frame chain for debuggers

    push dword [esp + 4]    ; argv
    push dword [esp + 4]    ; argc (esp moved by the push above)
    call main

    push eax
    call exit               ; flushes stdio, then _exit

.hang:                      ; exit never returns
    jmp .hang
