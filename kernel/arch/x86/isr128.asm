[BITS 32]

global isr128
extern syscall_handler

section .text

isr128:

    cli

    push dword 0        ; err_code
    push dword 128      ; int_no

    pusha

    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp
    call syscall_handler
    add esp, 4

    pop gs
    pop fs
    pop es
    pop ds

    popa

    add esp, 8          ; int_no + err_code 제거

    iretd