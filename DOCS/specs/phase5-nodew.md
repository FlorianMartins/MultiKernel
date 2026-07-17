# SPEC — Phase 5 : Node-W, chargeur PE & surface NT minimale

> Statut : `IMPLEMENTED (bringup)`. Portée : faire tourner **Node-W** en parallèle de
> Node-L, charger un exécutable **PE/COFF** natif, servir un sous-ensemble d'appels `Nt*`,
> réaliser une **I/O croisée via IPC** (Node-W front-end ↔ Node-L back-end), et **prouver
> le confinement** (un `.exe` Node-W qui plante n'affecte pas Node-L). cf. security-model C5.

## 1. Rôle & intégration

1er cœur Node-W activé → rôle **NODEW_KERNEL** (`smp.c`) → `node_w_main()`. Node-L et
Node-W tournent **simultanément** (deux OS souverains sur des cœurs disjoints). Le
Coordinator reste en monitor et observe les deux heartbeats.

## 2. Pagination propre (`NODE-W/mm`)

Fenêtre Node-W `[128 MiB, 160 MiB)` = `[0x8000000, 0xA000000)`. Tables propres :
- `[0, 32 MiB)` superviseur (image noyau, dont les rings IPC partagés).
- fenêtre Node-W mappée user (U=1) : image PE + pile user.
Le reste (dont la fenêtre Node-L) non-présent → accès croisé = #PF (isolation Phase 2).

## 3. Chargeur PE/COFF (`LOADERS/pe`)

Valide `MZ` (DOS), suit `e_lfanew`, valide `PE\0\0`, COFF (Machine=0x8664), Optional
header PE32+ (Magic=0x20b) : `ImageBase`, `AddressOfEntryPoint`, `SizeOfImage`. Copie
chaque section à `ImageBase + VirtualAddress` (borné à la fenêtre), zéro-remplit le
`VirtualSize > SizeOfRawData`. Applique la **table de relocations de base** (`.reloc`,
type `IMAGE_REL_BASED_DIR64`) si la base de chargement ≠ `ImageBase` (ici delta=0 car on
charge à `ImageBase`, mais le code de reloc est présent et exercé). Renvoie l'entrée
(`ImageBase + AddressOfEntryPoint`). **Fail-closed** : tout offset/taille hors bornes → rejet.

## 4. Surface NT (`NODE-W/ntdll` + `NODE-W/executive`, `int 0x2e`)

ABI : numéro `Nt*` dans `rax`, args `rdi/rsi/rdx` (SysV-like — le vrai NT/x64 utilise
`syscall` + `rcx/rdx/r8/r9` ; simplifié ici pour réutiliser l'infra, documenté). Vecteur
**`int 0x2e`** (vecteur syscall NT historique), gate DPL3.
Appels : `NtDisplayString(1)`, `NtStorageWrite(2, lba, buf, len)`,
`NtStorageRead(3, lba, buf, len)`, `NtTerminateProcess(4, code)`.

## 5. I/O croisée via IPC (split-driver, `IPC/channels`)

Deux rings SPSC dédiés dans la SHM : **req W→L** et **resp L→W** (réutilise `ipc_ring`).
- Node-W (front-end) : `NtStorageWrite` empaquette `{op, lba, len, payload}` → ring req →
  attend la réponse (borné TSC).
- Node-L (back-end) : après son bringup, sa boucle idle **poll le ring req**, écrit dans
  un « disque » simulé (buffer SHM), calcule un checksum, répond `{status, checksum}` → ring resp.
- `NtStorageRead` relit et Node-W **vérifie le round-trip** (checksum/contenu).
- **Sécurité** : le back-end borne tout `lba/len/offset` venant de Node-W (fail-closed) ;
  jamais de pointeur cross-domaine déréférencé — on ne passe que des données copiées.

## 6. Confinement (test renforcé, security-model C5)

Nominal : le `.exe` se termine par `NtTerminateProcess(0)`. Fault-injection (`NODEW_CRASH=1`)
: le `.exe` déréférence une adresse hors de sa fenêtre → #PF sur le cœur Node-W, capturé,
cœur gelé — **et Node-L continue** (heartbeat L progresse). Prouve qu'un nœud vérolé est
confiné.

## 7. Vérification QEMU (`run_phase5.sh`, superset 1–4)

Node-W : PE chargé (sections/entry loggés), ring 3 atteint, `Nt*` servis, `NtDisplayString`
imprime la bannière PE, **I/O croisée round-trip OK** (checksum vérifié), `NtTerminateProcess`
capturé, heartbeat W vu par le BSP. + Node-L toujours vivant (les deux OS en parallèle).
Fault-injection : build `NODEW_CRASH=1` → `run_phase5_crash.sh` prouve la survie de Node-L.
`make ci` vert (aucune dépendance croisée). Le noyau imprime ses `ASSERT ... : PASS`.
