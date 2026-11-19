ENTRY(_start)

SECTIONS
{
    . = 0xC0010000;

    .text : AT(ADDR(.text) - 0xC0000000)
    {
        *(.text)
    }

    .rodata : AT(ADDR(.rodata) - 0xC0000000)
    {
        *(.rodata)
    }

    .data : AT(ADDR(.data) - 0xC0000000)
    {
        *(.data)
    }

    .bss : AT(ADDR(.bss) - 0xC0000000)
    {
        *(COMMON)
        *(.bss)
    }

    _kernel_end = .;
}

/* Stage1은 물리 0x10000부터 로드. BSS까지 EBDA(0x9F000~) 아래에 있어야 함 */
ASSERT(_kernel_end - 0xC0000000 <= 0x9F000, "kernel image (incl. BSS) overlaps EBDA")