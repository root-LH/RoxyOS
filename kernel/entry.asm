[BITS 32]

global _start
global stack_top

extern kernel_main

KERNEL_VIRTUAL_BASE equ 0xC0000000
BOOT_PD_INDEX       equ (KERNEL_VIRTUAL_BASE >> 22)

section .text

_start:
    mov ecx, 0

.fill_pt:
    mov eax, ecx
    shl eax, 12                             ; ecx * 4096 (4KB)
    or eax, 0x003                           ; PAGE_PRESENT | PAGE_WRITABLE
    mov [boot_page_table - KERNEL_VIRTUAL_BASE + ecx * 4], eax
    inc ecx
    cmp ecx, 1024
    jl .fill_pt

    mov eax, boot_page_table - KERNEL_VIRTUAL_BASE
    or eax, 0x003                           ; PAGE_PRESENT | PAGE_WRITABLE
    mov [boot_page_directory - KERNEL_VIRTUAL_BASE], eax                     ; Index 0
    mov [boot_page_directory - KERNEL_VIRTUAL_BASE + BOOT_PD_INDEX * 4], eax ; Index 768

    mov eax, boot_page_directory - KERNEL_VIRTUAL_BASE
    mov cr3, eax

    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    lea eax, [higher_half_entry]
    jmp eax

higher_half_entry:
    mov esp, stack_top
    mov ebp, 0
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .data
align 4096
boot_page_directory:
    times 1024 dd 0

align 4096
boot_page_table:
    times 1024 dd 0

section .bss

align 16
stack_bottom:
    resb 16384          ; 16KB

stack_top: