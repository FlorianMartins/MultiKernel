; NEXUS-OS — stubs d'exceptions (vecteurs 0..31) et chargement d'IDT.
; Assemblé en ELF64, linké normalement.
BITS 64

extern exc_handler
extern g_doorbell_recv
global load_idt
global isr_table
global isr_doorbell

section .text

; Handler du doorbell (IPI fixed vecteur 0x41) : pose un flag, EOI, iretq.
; Écrit entièrement en asm -> pas de contrainte d'alignement ABI d'appel C.
isr_doorbell:
    push rax
    mov byte [rel g_doorbell_recv], 1
    mov eax, 0xFEE000B0          ; LAPIC EOI (zero-extend -> rax)
    mov dword [rax], 0
    pop rax
    iretq

; Cadre uniforme sur la pile : [rsp] = vecteur, [rsp+8] = code d'erreur.
%macro ISR_NOERR 1
global isr%1
isr%1:
    push 0            ; code d'erreur factice
    push %1           ; vecteur
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push %1           ; vecteur (code d'erreur déjà empilé par le CPU)
    jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

isr_common:
    mov rdi, [rsp]        ; vecteur      -> arg1
    mov rsi, [rsp + 8]    ; code erreur  -> arg2
    mov rdx, cr2          ; adresse fautive -> arg3
    and rsp, -16          ; alignement ABI (noreturn : cadre abandonné)
    call exc_handler
.hang:
    cli
    hlt
    jmp .hang

; void load_idt(void *idt_ptr)
load_idt:
    lidt [rdi]
    ret

section .rodata
align 16
isr_table:
%assign i 0
%rep 32
    dq isr %+ i
%assign i i+1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
