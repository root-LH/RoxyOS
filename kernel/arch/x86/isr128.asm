[BITS 32]

global isr128
extern syscall_handler
extern scheduler_need_resched
extern scheduler_clear_resched
extern process_reschedule_from_trap

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

    call scheduler_need_resched

    test eax, eax
    jz .no_resched

    push esp
    call process_reschedule_from_trap
    add esp, 4

    mov esp, eax

    call scheduler_clear_resched

.no_resched:

    pop gs
    pop fs
    pop es
    pop ds

    popa

    add esp, 8

    iretd