; NEXUS-OS HAL — stubs d'IRQ (clavier, timer ignoré, spurious).
; Sauvegardent les registres volatils, appellent le handler C, EOI, iretq.
BITS 64

extern kbd_handle
global isr_keyboard
global isr_timer_ignore
global isr_spurious

%define LAPIC_EOI 0xFEE000B0

section .text

; sauvegarde/restauration des registres volatils autour d'un appel C
%macro PUSH_VOL 0
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
%endmacro
%macro POP_VOL 0
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
%endmacro

; --- IRQ clavier (vecteur 0x21) : kbd_handle() fait l'EOI lui-même ---
isr_keyboard:
    PUSH_VOL
    cld
    call kbd_handle
    POP_VOL
    iretq

; --- IRQ timer (vecteur 0x20) : ignorée, mais EOI obligatoire ---
isr_timer_ignore:
    push rax
    mov eax, LAPIC_EOI
    mov dword [rax], 0
    pop rax
    iretq

; --- interruption spurious (vecteur 0xFF) : pas d'EOI ---
isr_spurious:
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
