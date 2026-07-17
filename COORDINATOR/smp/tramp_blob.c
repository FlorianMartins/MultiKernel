/* NEXUS-OS — incorpore le blob binaire du trampoline AP dans le noyau.
 * Le BSP le recopie à la physique 0x8000 avant de réveiller les AP. */
__asm__(
    ".section .rodata\n"
    ".balign 16\n"
    ".global tramp_blob_start\n"
    "tramp_blob_start:\n"
    ".incbin \"build/trampoline.bin\"\n"
    ".global tramp_blob_end\n"
    "tramp_blob_end:\n"
    ".previous\n"
);
