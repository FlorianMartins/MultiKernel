; NEXUS-OS — trampoline de réveil AP.
; Blob assemblé à plat (nasm -f bin), copié à la physique 0x8000 par le BSP.
; Réveillé par SIPI (vecteur 0x08) : real mode 16b -> protégé 32b -> long 64b -> ap_main.
; Bloc de paramètres à 0x9000, patché par le BSP avant chaque réveil.

BITS 16
ORG 0x8000

CODE64_SEL  equ 0x08
DATA_SEL    equ 0x10
CODE32_SEL  equ 0x18

PARAM_CR3    equ 0x9000
PARAM_STACK  equ 0x9008
PARAM_ENTRY  equ 0x9010
PARAM_FLAG   equ 0x9018

tramp_start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax

    lgdt [gdt_ptr]                  ; GDT locale (base linéaire 0x80xx)

    mov eax, cr0
    or  eax, 1                      ; PE
    mov cr0, eax
    jmp CODE32_SEL:pm32             ; -> mode protégé 32 bits

BITS 32
pm32:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov eax, [PARAM_CR3]           ; CR3 du domaine (fourni par le BSP)
    mov cr3, eax

    mov eax, cr4
    or  eax, 1 << 5                 ; CR4.PAE
    mov cr4, eax

    mov ecx, 0xC0000080            ; EFER
    rdmsr
    or  eax, 1 << 8                 ; EFER.LME (long mode)
    or  eax, 1 << 11                ; EFER.NXE (bit NX / W^X)
    wrmsr

    mov eax, cr0
    or  eax, 1 << 31                ; CR0.PG
    mov cr0, eax

    jmp CODE64_SEL:lm64            ; -> long mode 64 bits

BITS 64
lm64:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rsp, [PARAM_STACK]         ; pile propre à cet AP
    mov dword [PARAM_FLAG], 1       ; signale au BSP : params consommés
    mov rax, [PARAM_ENTRY]         ; = &ap_main
    call rax
.hang:
    cli
    hlt
    jmp .hang

; --- GDT locale au trampoline ---
align 16
gdt:
    dq 0x0000000000000000          ; 0x00 null
    dq 0x00AF9A000000FFFF          ; 0x08 code64 (L=1)
    dq 0x00CF92000000FFFF          ; 0x10 data
    dq 0x00CF9A000000FFFF          ; 0x18 code32
gdt_end:
gdt_ptr:
    dw gdt_end - gdt - 1
    dd gdt                          ; base linéaire 32 bits (< 16 MiB)
