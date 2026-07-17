; NEXUS-OS Node-W — exécutable PE32+ natif fabriqué à la main (nasm -f bin).
; S'exécute en ring 3 sur un cœur Node-W. Ne parle qu'à la surface Nt* via `int 0x2e`.
; Code position-indépendant (RIP-relative) -> aucune relocation nécessaire.
;
; ABI syscall NT (simplifiée) : rax=numéro Nt, args rdi/rsi/rdx ; retour rax.
;   NtDisplayString   = 1  (rdi=buf, rsi=len)
;   NtStorageWrite    = 2  (rdi=lba, rsi=buf, rdx=len)  -> I/O croisée via IPC
;   NtStorageRead     = 3  (rdi=lba, rsi=buf, rdx=len)
;   NtTerminateProcess= 4  (rdi=code)
;
; Build : nasm -f bin -DIMAGEBASE... ; option -dNODEW_CRASH pour le test de confinement.

BITS 64

IMAGE_BASE   equ 0x8400000
TEXT_RVA     equ 0x1000
FILE_ALIGN   equ 0x200
SECT_ALIGN   equ 0x1000

NT_DISPLAY   equ 1
NT_STGWRITE  equ 2
NT_STGREAD   equ 3
NT_TERMINATE equ 4

; ============================ EN-TÊTE DOS ============================
dos_start:
    dw 0x5A4D                       ; "MZ"
    times 0x3C-($-dos_start) db 0
    dd pe_start                     ; e_lfanew

; ============================ EN-TÊTE PE ============================
pe_start:
    dd 0x00004550                   ; "PE\0\0"
    ; --- COFF header ---
    dw 0x8664                       ; Machine x86-64
    dw 1                            ; NumberOfSections
    dd 0                            ; TimeDateStamp
    dd 0                            ; PointerToSymbolTable
    dd 0                            ; NumberOfSymbols
    dw opt_end - opt_start          ; SizeOfOptionalHeader
    dw 0x0022                       ; Characteristics: EXECUTABLE | LARGE_ADDRESS_AWARE

; --- Optional header (PE32+) ---
opt_start:
    dw 0x020B                       ; Magic PE32+
    db 0, 0                         ; linker maj/min
    dd text_filesz                  ; SizeOfCode
    dd 0                            ; SizeOfInitializedData
    dd 0                            ; SizeOfUninitializedData
    dd TEXT_RVA                     ; AddressOfEntryPoint (RVA)
    dd TEXT_RVA                     ; BaseOfCode
    dq IMAGE_BASE                   ; ImageBase
    dd SECT_ALIGN                   ; SectionAlignment
    dd FILE_ALIGN                   ; FileAlignment
    dw 0,0,0,0                      ; OS/Image version
    dw 1,0                          ; Subsystem version
    dd 0                            ; Win32VersionValue
    dd SECT_ALIGN + SECT_ALIGN      ; SizeOfImage (0x1000 headers + 0x1000 .text)
    dd FILE_ALIGN                   ; SizeOfHeaders
    dd 0                            ; CheckSum
    dw 1                            ; Subsystem = NATIVE
    dw 0                            ; DllCharacteristics
    dq 0x10000, 0x10000            ; StackReserve, StackCommit
    dq 0x10000, 0x10000            ; HeapReserve, HeapCommit
    dd 0                            ; LoaderFlags
    dd 16                           ; NumberOfRvaAndSizes
    times 16 dd 0, 0                ; 16 data directories (aucune)
opt_end:

; --- Section header : .text ---
    db ".text", 0, 0, 0             ; Name (8 octets)
    dd text_vsize                   ; VirtualSize
    dd TEXT_RVA                     ; VirtualAddress (RVA)
    dd text_filesz                  ; SizeOfRawData
    dd FILE_ALIGN                   ; PointerToRawData (offset fichier)
    dd 0, 0                         ; reloc/linenum ptr
    dw 0, 0                         ; num reloc/linenum
    dd 0x60000020                   ; Characteristics: CODE|EXEC|READ

; --- padding jusqu'à FILE_ALIGN (début de .text dans le fichier) ---
    times FILE_ALIGN-($-dos_start) db 0

; ============================ SECTION .text ============================
text_start:
_start:
    ; 1) NtDisplayString(banner)
    lea rdi, [rel banner]
    mov rsi, banner_len
    mov rax, NT_DISPLAY
    int 0x2e

%ifdef NODEW_CRASH
    ; --- test de confinement : accès hors fenêtre Node-W (dans Node-L) -> #PF ---
    lea rdi, [rel crashing]
    mov rsi, crashing_len
    mov rax, NT_DISPLAY
    int 0x2e
    mov rax, [0x4000000]            ; fenêtre Node-L : non mappée -> faute
    ; (jamais atteint)
%else
    ; 2) I/O croisée : NtStorageWrite(lba=7, payload) -> Node-L back-end
    mov rdi, 7                      ; lba
    lea rsi, [rel payload]
    mov rdx, payload_len
    mov rax, NT_STGWRITE
    int 0x2e                        ; retour rax = checksum vu par le back-end (ou -1)

    ; le noyau Node-W vérifie le checksum ; on affiche juste un accusé
    lea rdi, [rel wrote]
    mov rsi, wrote_len
    mov rax, NT_DISPLAY
    int 0x2e
%endif

    ; 3) NtTerminateProcess(0)
    mov rdi, 0
    mov rax, NT_TERMINATE
    int 0x2e
.hang:
    jmp .hang                       ; sécurité

; --- données (dans .text, référencées en RIP-relative) ---
banner:
    db 10, "  ============================================", 10
    db "   Prism Node-W  --  PE32+ native (ring 3, NT)", 10
    db "  ============================================", 10
    db "[pe] NtDisplayString OK", 10
banner_len equ $ - banner

payload:
    db "PING-FROM-NODE-W:block7"
payload_len equ $ - payload

wrote:
    db "[pe] NtStorageWrite -> Node-L back-end OK", 10
wrote_len equ $ - wrote

crashing:
    db "[pe] (crash mode) touching Node-L RAM ...", 10
crashing_len equ $ - crashing

text_end:
text_filesz equ text_end - text_start
text_vsize  equ text_end - text_start

    ; padding du fichier jusqu'à FILE_ALIGN (taille de .text sur disque)
    times FILE_ALIGN-((($-dos_start)) % FILE_ALIGN) db 0
