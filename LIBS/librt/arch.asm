; NEXUS-OS librt — primitives d'architecture partagées (x86_64).
; Communes à Node-L et Node-W (code arch-level, PAS spécifique à un domaine) :
; passage ring 3, changement de contexte, setjmp/longjmp noyau, chargement GDT/TSS.
; Convention GDT (identique aux deux nœuds) :
;   0x08 kcode  0x10 kdata  0x18 ucode(DPL3)  0x20 udata(DPL3)  0x28 TSS
BITS 64

global enter_user
global switch_context
global kctx_save
global kctx_restore
global gdt_flush
global tss_flush

section .text

; --- void enter_user(u64 entry (rdi), u64 user_stack (rsi), u64 rflags (rdx)) ---
; Bascule en ring 3. rflags permet à Node-L d'activer IF=1 (0x202) pour recevoir les
; IRQ (clavier) en ring 3 ; Node-W passe 0x002 (IF=0).
enter_user:
    mov ax, 0x23                    ; udata | RPL3
    mov ds, ax
    mov es, ax
    push 0x23                       ; SS
    push rsi                        ; RSP
    push rdx                        ; RFLAGS (fourni par l'appelant)
    push 0x1B                       ; CS (ucode | RPL3)
    push rdi                        ; RIP
    iretq

; --- void switch_context(u64 *save_old_rsp (rdi), u64 new_rsp (rsi)) ---
switch_context:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp
    mov rsp, rsi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

; --- long kctx_save(u64 *buf) : setjmp minimal (renvoie 0 à l'appel direct) ---
; buf : [0]rbx [8]rbp [16]r12 [24]r13 [32]r14 [40]r15 [48]rsp [56]rip
kctx_save:
    mov [rdi + 0],  rbx
    mov [rdi + 8],  rbp
    mov [rdi + 16], r12
    mov [rdi + 24], r13
    mov [rdi + 32], r14
    mov [rdi + 40], r15
    lea rax, [rsp + 8]
    mov [rdi + 48], rax
    mov rax, [rsp]
    mov [rdi + 56], rax
    xor eax, eax
    ret

; --- void kctx_restore(u64 *buf, long val) : longjmp ---
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

; --- void gdt_flush(struct gdt_ptr *p) : lgdt + recharge les sélecteurs ---
gdt_flush:
    lgdt [rdi]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    lea rax, [rel .reload]
    push qword 0x08
    push rax
    retfq
.reload:
    ret

; --- void tss_flush(u16 sel) ---
tss_flush:
    mov ax, di
    ltr ax
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
