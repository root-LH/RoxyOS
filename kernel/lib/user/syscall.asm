[BITS 32]

global sys_write
global sys_getchar
global sys_open
global sys_read
global sys_close
global sys_exit
global sys_sleep

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
    int 0x80
    ret

sys_sleep:
    mov eax, 6
    mov ebx, [esp+4]    ; ticks
    int 0x80
    ret
