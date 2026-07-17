; NEXUS-OS Node-L — entrée syscall spécifique (int 0x80 -> syscall_dispatch).
; Les primitives arch génériques (enter_user, switch_context, kctx_*, gdt/tss_flush)
; sont partagées dans LIBS/librt/arch.asm.
BITS 64

extern syscall_dispatch          ; u64 syscall_dispatch(num, a1, a2, a3)
global syscall_entry

section .text

; --- int 0x80 : rax=num, rdi=a1, rsi=a2, rdx=a3 ; retour dans rax ---
; Préserve TOUS les registres volatils du ring 3 : du point de vue de l'appelant,
; seul rax (valeur de retour) change. Indispensable — sinon l'inline-asm userland
; réutilise rdi/rsi/rdx qu'il croit intacts alors que le dispatch C les a écrasés.
syscall_entry:
    push rdi
    push rsi
    push rdx
    push rcx
    push r8
    push r9
    push r10
    push r11
    ; réordonne vers l'ABI C : dispatch(rdi=num, rsi=a1, rdx=a2, rcx=a3)
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax
    sub rsp, 8                     ; aligne la pile sur 16 avant l'appel
    call syscall_dispatch
    add rsp, 8
    ; rax = valeur de retour -> conservée ; on restaure le reste
    pop r11
    pop r10
    pop r9
    pop r8
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
