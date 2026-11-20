[BITS 32]

global process_start_asm

; void process_start_asm(registers_t *frame);

process_start_asm:

    mov esp, [esp+4]

    pop gs
    pop fs
    pop es
    pop ds

    popa

    add esp, 8          ; int_no, err_code

    iretd
