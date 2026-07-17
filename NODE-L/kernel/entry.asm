; NEXUS-OS Node-L — points d'entrée bas niveau (ELF64).
;  - syscall_entry : handler int 0x80 (ring3 -> ring0 via TSS.RSP0)
;  - enter_user    : bascule en ring 3 (iretq)
;  - switch_context: changement de contexte coopératif
BITS 64

extern syscall_dispatch          ; u64 syscall_dispatch(num, a1, a2, a3)
global syscall_entry
global enter_user
global switch_context
global kctx_save
global kctx_restore
global gdt_flush
global tss_flush

section .text

; --- void gdt_flush(struct gdt_ptr *p) : lgdt + recharge les sélecteurs ---
gdt_flush:
    lgdt [rdi]
    mov ax, 0x10                  ; kernel data
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    ; recharge CS via un far return
    lea rax, [rel .reload]
    push qword 0x08               ; kernel code
    push rax
    retfq
.reload:
    ret

; --- void tss_flush(u16 sel) ---
tss_flush:
    mov ax, di
    ltr ax
    ret

; --- long kctx_save(u64 *buf) : setjmp minimal (0 à l'appel direct) ---
; buf : [0]rbx [8]rbp [16]r12 [24]r13 [32]r14 [40]r15 [48]rsp [56]rip
kctx_save:
    mov [rdi + 0],  rbx
    mov [rdi + 8],  rbp
    mov [rdi + 16], r12
    mov [rdi + 24], r13
    mov [rdi + 32], r14
    mov [rdi + 40], r15
    lea rax, [rsp + 8]           ; rsp de l'appelant (après le ret)
    mov [rdi + 48], rax
    mov rax, [rsp]               ; adresse de retour
    mov [rdi + 56], rax
    xor eax, eax
    ret

; --- void kctx_restore(u64 *buf, long val) : longjmp (revient dans kctx_save avec val) ---
kctx_restore:
    mov rbx, [rdi + 0]
    mov rbp, [rdi + 8]
    mov r12, [rdi + 16]
    mov r13, [rdi + 24]
    mov r14, [rdi + 32]
    mov r15, [rdi + 40]
    mov rsp, [rdi + 48]
    mov rax, rsi
    test rax, rax
    jnz .nz
    mov rax, 1
.nz:
    jmp qword [rdi + 56]

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
    sub rsp, 8                     ; RSP%16: 8(entrée)+64(push)=... -> aligne à 0 avant call
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

; --- void enter_user(u64 entry (rdi), u64 user_stack (rsi)) ---
; Bascule en ring 3 via iretq. Sélecteurs user : CS=0x1B, SS/DS=0x23.
enter_user:
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    push 0x23                     ; SS
    push rsi                      ; RSP (pile user)
    push 0x002                    ; RFLAGS (IF=0 : Node-L n'utilise pas d'IRQ en ring3)
    push 0x1B                     ; CS (user code | RPL3)
    push rdi                      ; RIP (entrée user)
    iretq

; --- void switch_context(u64 *save_old_rsp (rdi), u64 new_rsp (rsi)) ---
switch_context:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp                ; sauve rsp de la tâche courante
    mov rsp, rsi                  ; charge rsp de la nouvelle tâche
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
