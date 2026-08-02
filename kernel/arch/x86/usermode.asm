[BITS 32]

global switch_to_user_mode
extern user_mode_entry

switch_to_user_mode:

    mov eax, [esp+4]      ; user stack top

    push dword 0x23       ; SS
    push eax              ; ESP

    pushfd

    push dword 0x1B       ; CS
    push dword user_mode_entry

    iretd