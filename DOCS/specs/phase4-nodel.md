# SPEC — Phase 4 : Node-L, premier OS souverain (POSIX minimal)

> Statut : `IMPLEMENTED (bringup)`. Portée : faire tourner un noyau **Node-L** sur son
> cœur, dans sa partition, avec pagination propre, ring 3, syscalls, ordonnanceur local,
> chargeur ELF, un userland qui s'exécute, et un heartbeat vers le Coordinator.

## 1. Rôle & intégration

Le 1er cœur Node-L activé reçoit le rôle **NODEL_KERNEL** (`smp.c`) et appelle
`node_l_main()`. Les autres cœurs gardent leurs rôles (IPC producteur si 2e cœur L,
isolation sinon). Le Coordinator (BSP) reste en mode monitor et **observe le heartbeat**
de Node-L (compteur en SHM), conformément à `ARCHITECTURE.md` §3.5.

## 2. Pagination propre (`NODE-L/mm`)

Node-L construit **ses propres** tables (PML4/PDPT/PD) dans sa partition et bascule `CR3` :
- `[0, 32 MiB)` : image noyau (code/données/piles) en **superviseur** (U=0) — le code
  Node-L continue de s'exécuter (identité).
- `[64 MiB, 96 MiB)` : fenêtre Node-L mappée **user-accessible** (U=1) — userland + pile user.
Ainsi le ring 3 ne peut pas toucher le noyau (U=0), preuve d'une séparation user/superviseur.

## 3. GDT / TSS / IDT (par cœur)

GDT Node-L : null, kernel code64 (0x08), kernel data (0x10), **user code64 (0x18, DPL3)**,
**user data (0x20, DPL3)**, **TSS (0x28)**. TSS.RSP0 = pile noyau (pour l'entrée syscall
depuis le ring 3). IDT : réutilise les stubs d'exception 0..31 + **gate `int 0x80` (DPL3)**.

## 4. Syscalls (`NODE-L/syscall`, `int 0x80`)

ABI : numéro dans `rax`, args `rdi/rsi/rdx`, retour `rax`. Implémentés :
`SYS_write(1)`, `SYS_getpid(2)`, `SYS_yield(3)`, `SYS_exit(4)`, `SYS_read(5)`.
Entrée : `syscall_entry` (asm) bascule sur RSP0, appelle `syscall_dispatch`, `iretq`.
`SYS_read` est **non bloquant** (renvoie 0 si pas d'octet) → jamais de hang sans entrée.

## 5. Ordonnanceur (`NODE-L/sched`)

Coopératif, round-robin, changement de contexte réel (`switch_context`, sauvegarde des
registres callee-saved). Démo : `node_l_main` crée 2 tâches noyau qui impriment et
`yield()` entre elles → preuve de commutation. (Préemption par timer = phase ultérieure.)

## 6. Chargeur ELF (`LOADERS/elf`) & userland (`NODE-L/userland`)

Le userland est un ELF64 statique **freestanding** (lié à `0x4400000`), incorporé au noyau
(`.incbin`). Le chargeur valide l'en-tête (magic, classe 64, type EXEC), copie les segments
`PT_LOAD` à leur `p_vaddr` (fenêtre user), zéro-remplit le `.bss`, puis `enter_user(entry,
sp)` bascule en **ring 3**. Le programme `init` fait : bannière via `SYS_write`, `SYS_getpid`,
un mini-shell **borné** (lit des lignes via `SYS_read`, commandes `help/echo/ps/exit`), puis
`SYS_exit`. Sécurité : le noyau borne toute copie ELF (offsets/tailles) — `fail-closed`
(cf. `DOCS/security-model.md`).

## 7. Heartbeat

Node-L incrémente `g_nodel_heartbeat` (SHM) en boucle idle ; le BSP le lit et affiche sa
progression + `ASSERT node-l alive: PASS`.

## 8. Règle CI (`TOOLS/ci`)

`check_deps.sh` échoue si un fichier `NODE-L/` référence `NODE-W/` (ou l'inverse) — la
frontière de domaine la plus dure du projet, vérifiée mécaniquement (`make ci`).

## 9. Vérification QEMU (`run_phase4.sh`, superset des Phases 1–3)

Contrôles : bascule CR3 Node-L OK ; 2 tâches ordonnancées ; userland en **ring 3** imprime
sa bannière + PID ; commandes shell exécutées (entrée injectée par pipe) ; `SYS_exit`
capturé ; heartbeat vu par le BSP. + Phases 1–3 toujours vertes. `make ci` vert.
Le noyau imprime ses `ASSERT ... : PASS` ; le script les grep et refuse `FAIL/FATAL/HANG`.
