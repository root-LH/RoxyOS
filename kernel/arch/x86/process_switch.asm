[BITS 32]

global process_switch_asm

; void process_switch_asm(context_t **old, context_t *new);

process_switch_asm:

    mov eax, [esp+4]      ; old context **
    mov edx, [esp+8]      ; new context *

    push ebp
    push ebx
    push esi
    push edi

    mov [eax], esp

    mov esp, edx

    pop edi
    pop esi
    pop ebx
    pop ebp

    ret