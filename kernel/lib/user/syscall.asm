[BITS 32]

global sys_write
global sys_getchar
global sys_open
global sys_read
global sys_close
global sys_exit

sys_write:
    mov eax, 0
    mov ebx, [esp+4]    ; str
    int 0x80
    ret

sys_getchar:
    mov eax, 1
    int 0x80
    ret

sys_open:
    mov eax, 2
    mov ebx, [esp+4]    ; path
    int 0x80
    ret

sys_read:
    mov eax, 3
    mov ebx, [esp+4]    ; fd
    mov ecx, [esp+8]    ; buffer
    mov edx, [esp+12]   ; size
    int 0x80
    ret

sys_close:
    mov eax, 4
    mov ebx, [esp+4]    ; fd
    int 0x80
    ret

sys_exit:
    mov eax, 5
    mov ebx, [esp+4]    ; status
    int 0x80

.loop:
    jmp .loop