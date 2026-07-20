[org 0x8000]
[bits 16]

start:

    ; -------------------
    ; A20 Enable
    ; -------------------

    in al,0x92
    or al,2
    out 0x92,al

    mov ax,0xB800
    mov es,ax

    mov byte [es:0],'A'
    mov byte [es:1],0x0F

    lgdt [gdt_descriptor]

    mov byte [es:2],'G'
    mov byte [es:3],0x0F

    cli

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp CODE_SEG:protected_mode

[bits 32]

protected_mode:

    mov ax, DATA_SEG

    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    jmp 0x08:0x10000

.hang:
    jmp .hang

%include "boot/gdt.inc"