[BITS 32]

global outb
global inb

outb:
    mov dx, [esp+4]
    mov al, [esp+8]
    out dx, al
    ret

inb:
    mov dx, [esp+4]
    xor eax, eax
    in al, dx
    ret

global outw
global inw

outw:
    mov dx, [esp+4]
    mov ax, [esp+8]
    out dx, ax
    ret

inw:
    mov dx, [esp+4]
    xor eax, eax
    in ax, dx
    ret