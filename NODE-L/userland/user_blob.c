/* NEXUS-OS — incorpore l'ELF du userland Node-L dans l'image noyau. */
__asm__(
    ".section .rodata\n"
    ".balign 16\n"
    ".global nodel_user_start\n"
    "nodel_user_start:\n"
    ".incbin \"build/nodel_user.elf\"\n"
    ".global nodel_user_end\n"
    "nodel_user_end:\n"
    ".previous\n"
);
