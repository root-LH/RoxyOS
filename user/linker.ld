/* User programs: loaded by kernel/elf_loader.c into their own address space. */
ENTRY(_start)

SECTIONS
{
    . = 0x08048000;

    .text : { *(.text.start) *(.text*) }
    .rodata : { *(.rodata*) }

    . = ALIGN(4096);
    .data : { *(.data*) }
    .bss : { *(COMMON) *(.bss*) }

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
