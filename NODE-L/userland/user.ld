/* NEXUS-OS Node-L userland — lié dans la fenêtre user (base 0x4400000 = 68 MiB). */
ENTRY(_start)

SECTIONS {
    . = 0x4400000;

    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(COMMON) *(.bss .bss.*) }

    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note .note.*) }
}
