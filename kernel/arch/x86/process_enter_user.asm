[BITS 32]

global process_enter_user_asm

; void process_enter_user_asm(user_context_t *context);

process_enter_user_asm:

    mov eax, [esp + 4]

    ; iret frame
    push dword [eax + 16]    ; SS
    push dword [eax + 12]    ; ESP
    push dword [eax + 8]     ; EFLAGS
    push dword [eax + 4]     ; CS
    push dword [eax + 0]     ; EIP

    iret