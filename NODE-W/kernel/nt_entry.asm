; NEXUS-OS Node-W — entrée syscall NT (int 0x2e -> nt_dispatch).
; Les primitives arch génériques sont partagées dans LIBS/librt/arch.asm.
BITS 64

extern nt_dispatch               ; u64 nt_dispatch(num, a1, a2, a3)
global nt_syscall_entry

section .text

; --- int 0x2e : rax=num Nt, rdi=a1, rsi=a2, rdx=a3 ; retour dans rax ---
; Préserve tous les registres volatils du ring 3 (même raison que Node-L).
nt_syscall_entry:
    push rdi
    push rsi
    push rdx
    push rcx
    push r8
    push r9
    push r10
    push r11
    mov rcx, rdx                   ; -> ABI C : nt_dispatch(rdi=num, rsi=a1, rdx=a2, rcx=a3)
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax
    sub rsp, 8
    call nt_dispatch
    add rsp, 8
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
