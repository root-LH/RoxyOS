[org 0x7C00]
[bits 16]

%include "boot/stage1.inc"

start:

    cli

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    sti

    ; BIOS가 전달한 부팅 드라이브 번호 저장
    mov [BOOT_DRIVE], dl

    ; ------------------------
    ; Load Stage2
    ; ------------------------

    mov ax, 0
    mov es, ax
    mov bx, 0x8000

    mov ah, 0x02
    mov al, 1          ; Stage2는 현재 1 Sector
    mov ch, 0
    mov cl, 2          ; Sector 2
    mov dh, 0
    mov dl, [BOOT_DRIVE]

    int 0x13
    jc disk_error

    ; ------------------------
    ; Load Kernel
    ; ------------------------

    mov ax, 0x1000
    mov es, ax
    xor bx, bx

    mov ah, 0x02
    mov al, KERNEL_SECTORS
    mov ch, 0
    mov cl, 3
    mov dh, 0
    mov dl, [BOOT_DRIVE]

    int 0x13
    jc disk_error

    jmp 0x0000:0x8000

disk_error:

    mov si,msg

.print:

    lodsb

    or al,al

    jz $

    mov ah,0x0E

    int 10h

    jmp .print

msg db "Disk Error",0

BOOT_DRIVE db 0

times 510-($-$$) db 0
dw 0xAA55