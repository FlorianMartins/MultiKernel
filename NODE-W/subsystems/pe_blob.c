/* NEXUS-OS — incorpore l'exécutable PE Node-W dans l'image noyau. */
__asm__(
    ".section .rodata\n"
    ".balign 16\n"
    ".global nodew_pe_start\n"
    "nodew_pe_start:\n"
    ".incbin \"build/hello_pe.exe\"\n"
    ".global nodew_pe_end\n"
    "nodew_pe_end:\n"
    ".previous\n"
);
