/* NEXUS-OS — script de link Phase 1. Noyau chargé à 1 MiB (identité v==p). */
ENTRY(_start)

SECTIONS {
    . = 1M;

    /* L'en-tête Multiboot2 doit apparaître tôt (< 32 Kio du début de fichier). */
    .boot ALIGN(8) : {
        KEEP(*(.multiboot_header))
    }

    .text ALIGN(4K) : {
        *(.text .text.*)
    }

    .rodata ALIGN(4K) : {
        *(.rodata .rodata.*)
    }

    .data ALIGN(4K) : {
        *(.data .data.*)
    }

    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss .bss.*)
    }

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note .note.*)
    }
}
