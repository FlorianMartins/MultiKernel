; NEXUS-OS — stub d'amorçage Phase 1
; GRUB (Multiboot2) nous livre en mode protégé 32 bits. On vérifie le CPU,
; on installe une pagination identité 4 GiB (huge pages 2 MiB), on passe en
; long mode et on saute dans kmain(magic, mbi).
; Assemblé par NASM en ELF64.

MB2_MAGIC   equ 0xE85250D6
MB2_ARCH    equ 0                    ; 0 = i386 (entrée en mode protégé 32 bits)
MB2_LOADER  equ 0x36D76289          ; magic déposé dans EAX par le chargeur

PRESENT     equ 1 << 0
WRITABLE    equ 1 << 1
HUGE_PAGE   equ 1 << 7

; ---------------------------------------------------------------------------
; En-tête Multiboot2 (doit tenir dans les 32 premiers Kio du fichier, aligné 8)
; ---------------------------------------------------------------------------
section .multiboot_header
align 8
mb_hdr_start:
    dd MB2_MAGIC
    dd MB2_ARCH
    dd mb_hdr_end - mb_hdr_start
    dd -(MB2_MAGIC + MB2_ARCH + (mb_hdr_end - mb_hdr_start))
align 8
    ; tag de fin { type=0, flags=0, size=8 }
    dw 0
    dw 0
    dd 8
mb_hdr_end:

; ---------------------------------------------------------------------------
; Tables de pages + pile (BSS, zéro-initialisées par le chargeur)
; ---------------------------------------------------------------------------
section .bss
align 4096
p4_table:  resb 4096                 ; PML4
p3_table:  resb 4096                 ; PDPT
p2_tables: resb 4096 * 4             ; 4 PD -> 4 GiB en pages de 2 MiB
align 16
stack_bottom:
    resb 16384                       ; 16 Kio de pile
stack_top:

; ---------------------------------------------------------------------------
; GDT 64 bits
; ---------------------------------------------------------------------------
section .rodata
align 8
gdt64:
    dq 0                                             ; descripteur nul
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)          ; exécutable, type=code/data, présent, long mode
.data: equ $ - gdt64
    dq (1<<41) | (1<<44) | (1<<47)                    ; inscriptible, type=code/data, présent
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

; ---------------------------------------------------------------------------
; Code 32 bits — point d'entrée
; ---------------------------------------------------------------------------
section .text
bits 32
global _start
extern kmain
_start:
    mov esp, stack_top
    mov edi, eax                     ; -> 1er arg (rdi) : magic Multiboot2
    mov esi, ebx                     ; -> 2e  arg (rsi) : pointeur MBI

    call check_multiboot
    call check_cpuid
    call check_long_mode
    call setup_page_tables
    call enable_paging

    lgdt [gdt64.pointer]
    jmp gdt64.code:long_mode_start   ; far jump -> segment 64 bits

; --- vérifications ---------------------------------------------------------
check_multiboot:
    cmp edi, MB2_LOADER
    jne .fail
    ret
.fail:
    mov al, '0'
    jmp error

check_cpuid:                         ; CPUID dispo si on peut inverser EFLAGS.ID (bit 21)
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je .fail
    ret
.fail:
    mov al, '1'
    jmp error

check_long_mode:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .fail                         ; pas de feuille étendue -> pas de long mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29                ; bit LM
    jz .fail
    ret
.fail:
    mov al, '2'
    jmp error

; --- pagination identité 4 GiB (2 MiB huge pages) --------------------------
setup_page_tables:
    ; PML4[0] -> PDPT
    mov eax, p3_table
    or  eax, PRESENT | WRITABLE
    mov [p4_table], eax
    mov dword [p4_table + 4], 0

    ; PDPT[0..3] -> les 4 PD
    mov eax, p2_tables
    or  eax, PRESENT | WRITABLE
    mov [p3_table + 0], eax
    mov dword [p3_table + 4], 0
    add eax, 4096
    mov [p3_table + 8], eax
    mov dword [p3_table + 12], 0
    add eax, 4096
    mov [p3_table + 16], eax
    mov dword [p3_table + 20], 0
    add eax, 4096
    mov [p3_table + 24], eax
    mov dword [p3_table + 28], 0

    ; Remplit 2048 PDE : entrée i -> adresse physique i * 2 MiB, huge page
    xor ecx, ecx
.fill:
    mov eax, 0x200000
    mul ecx                          ; edx:eax = 2 MiB * i  (edx=0 dans notre plage)
    or  eax, PRESENT | WRITABLE | HUGE_PAGE
    mov [p2_tables + ecx*8], eax
    mov dword [p2_tables + ecx*8 + 4], 0
    inc ecx
    cmp ecx, 2048
    jb .fill
    ret

; --- activation PAE + long mode + paging -----------------------------------
enable_paging:
    mov eax, p4_table
    mov cr3, eax                     ; charge PML4

    mov eax, cr4
    or  eax, 1 << 5                  ; CR4.PAE
    mov cr4, eax

    mov ecx, 0xC0000080             ; MSR EFER
    rdmsr
    or  eax, 1 << 8                  ; EFER.LME (long mode)
    or  eax, 1 << 11                 ; EFER.NXE (bit NX / W^X)
    wrmsr

    mov eax, cr0
    or  eax, 1 << 31                 ; CR0.PG
    mov cr0, eax
    ret

; --- erreur fatale 32 bits : "ERR:X" en haut de l'écran VGA puis halt -------
error:
    mov dword [0xb8000], 0x4f524f45  ; "ER"
    mov dword [0xb8004], 0x4f3a4f52  ; "R:"
    mov byte  [0xb8008], al
    mov byte  [0xb8009], 0x4f
.hang:
    hlt
    jmp .hang

; ---------------------------------------------------------------------------
; Code 64 bits — après le passage en long mode
; ---------------------------------------------------------------------------
bits 64
long_mode_start:
    mov ax, gdt64.data
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; rdi = magic, rsi = mbi (déjà positionnés en 32 bits, bits hauts à zéro)
    call kmain

.hang:
    cli
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
