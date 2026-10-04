# Feuille de route — personnalité NT de Node-W (phases 12 → 19)

> Statut : **proposition, à valider par Florian avant tout code** (règle du dépôt).
> Date : 2026-10-04. Point de départ : Node-W = chargeur PE32+ minimal + 4 appels
> `Nt*` via `int 0x2e` + I/O croisée vers Node-L (Phase 5), ~770 lignes.

## 1. La question posée

« Peut-on faire un noyau multiple qui lance des `.exe` en natif, avec les services
Windows et un noyau compatible avec les anti-cheats qui s'appuient sur le noyau
Windows ? »

La réponse diffère selon la couche visée.

| Objectif | Faisable ? | Pourquoi |
|---|---|---|
| Lancer des `.exe` user-mode en natif | **Oui** | Wine/Proton le prouvent depuis des années (en traduisant l'API). Node-W peut aller plus loin et exposer un **vrai noyau NT** (appels système `Nt*`, objets, handles, mémoire virtuelle) sous des DLL user-mode. |
| Services Windows (SCM, `services.exe`, svchost) | **Oui** | Ce sont des processus user-mode qui parlent au noyau par des appels `Nt*` et de l'ALPC. Wine possède déjà un SCM. |
| Pilotes `.sys` génériques (classe stockage, HID, filtre simple) | **En partie** | Il faut réimplémenter les exports de `ntoskrnl.exe`/`hal.dll` (Io*, Ke*, Mm*, Ob*, Ps*, Ex*, Rtl*). ReactOS le fait. Chaque pilote élargit le périmètre. |
| Anti-cheat noyau (Vanguard, FACEIT, EAC/BattlEye en mode noyau, Ricochet) | **Non, sans l'éditeur** | Voir §2. |
| Jeux à anti-cheat **user-mode** compatibles Proton (EAC/BattlEye avec le support Linux activé par le studio) | **Oui, à terme** | Ce modèle repose sur la coopération de l'éditeur ; c'est la voie que Prism doit viser. |

## 2. Pourquoi l'anti-cheat noyau est hors d'atteinte (et hors périmètre)

Un anti-cheat noyau ne se contente pas d'appeler des fonctions documentées. Il vérifie
activement qu'il tourne sur **un Windows authentique et intègre** :

1. **Attestation matérielle** : Vanguard et FACEIT exigent TPM 2.0 + Secure Boot. Le TPM
   mesure la chaîne de démarrage (PCR) ; une chaîne Prism produit d'autres mesures, et
   le serveur de l'éditeur le voit. Ces mesures ne se falsifient pas sans casser le TPM.
2. **Structures non documentées** : offsets d'`EPROCESS`/`ETHREAD` propres à chaque build,
   présence de PatchGuard, état de Code Integrity (`ci.dll`), VBS/HVCI, hachage de
   `ntoskrnl.exe` en mémoire.
3. **Modèle de menace** : un noyau qui *imite* Windows tout en étant contrôlé par
   quelqu'un d'autre, c'est exactement l'outil d'un tricheur. Les éditeurs le traitent comme
   hostile, ce qui entraîne des bannissements.

Faire croire à un anti-cheat qu'il tourne sur Windows, c'est **contourner une mesure de
protection** : violation des CGU, bannissement des comptes, et contournement au sens du
droit (DMCA §1201, directive EUCD art. 6). **Prism ne construira pas cette couche.**

**Voie légitime**, celle qu'ont prise Valve, Epic et BattlEye avec Proton en 2021 :
rendre Prism **certifiable** et offrir aux éditeurs une API d'attestation *native Prism*
(démarrage mesuré, isolation prouvée, IOMMU, W^X). L'architecture sans hyperviseur de
Prism devient alors un argument, plus une ruse. C'est la Phase 19.

## 3. Choix d'architecture : noyau NT réel + DLL user-mode réutilisées

```
 .exe / services.exe / jeu
 ─────────────────────────────── ring 3 (Node-W)
  kernel32 / kernelbase / user32 / gdi32 / ucrtbase   ← DLL PE de Wine (LGPL), réutilisées
  ntdll.dll Prism                                      ← NOTRE ntdll : stubs `syscall`
 ─────────────────────────────── instruction `syscall` (MSR LSTAR)
  Exécutif NT Prism (ring 0, Node-W)
  Ob (objets/handles/namespace) · Mm (VAD, sections) · Ps (process/threads, TEB/PEB)
  Ke (ordonnanceur préemptif, APC/DPC, waits) · Io (IRP, pilotes) · Cm (registre)
 ─────────────────────────────── IPC ring (Phase 3)
  Node-L : système de fichiers, réseau, GPU (back-ends split-driver)
```

Pourquoi ce découpage :
- On écrit nous-mêmes **la seule partie qui fait l'identité de Node-W** : le noyau NT,
  soit ~470 appels système dont une centaine suffisent pour les programmes courants.
- Les DLL Win32 de Wine sont compilées **en PE** depuis Wine 5 ; elles appellent `ntdll`.
  Leur côté Unix (`__wine_unix_call`) est remplacé par des appels système Prism. La
  frontière est nette et la licence (LGPL, DLL chargées dynamiquement) le permet.
- La toolchain visée est `mingw-w64` (disponible : 11.0.1, à installer) : elle produit de
  **vrais** `.exe` et `.dll`, ce qui met fin aux PE assemblés à la main.

## 4. Plan de vol

Chaque phase garde les règles habituelles : spec dédiée, test QEMU déterministe, matrice
verte, fault-injection (« tester le test »), toute donnée venant du ring 3 bornée et
fail-closed (`security-model.md` C2/C5).

| Phase | Livrable | Preuve attendue |
|---|---|---|
| **12 — Noyau NT : objets, mémoire, appels système** | `syscall`/`sysret` (LSTAR/STAR/SFMASK) à la place de `int 0x2e` ; Object Manager (table de handles, refcount, `NtClose`, `NtDuplicateObject`, namespace `\`, `\??`, `\Device`) ; Mm avec VAD (`NtAllocate/Free/Protect/QueryVirtualMemory`, W^X imposé) ; TEB/PEB via `GS` base | Test PE : alloue, protège, libère ; un handle invalide ou forgé est refusé ; RW→RX sans passer par W+X ; fuzz des arguments |
| **13 — Chargeur PE complet + ntdll Prism** | imports/exports, forwarders, ApiSet (`api-ms-win-*`), TLS callbacks, `LdrLoadDll`, relocations complètes ; `ntdll.dll` Prism compilée en mingw | Un `hello.exe` **compilé par mingw** (CRT incluse) affiche son texte et sort avec le bon code |
| **14 — Processus, threads, synchronisation** | ordonnanceur préemptif Node-W (timer LAPIC), `NtCreateThreadEx`, events/mutants/semaphores, `NtWaitForMultipleObjects`, APC, plusieurs processus avec un CR3 chacun | Programme multi-thread : producteur/consommateur Win32, pas d'interblocage sur la matrice |
| **15 — Fichiers, registre, console** | `NtCreateFile`/`NtReadFile`/`NtWriteFile` via le canal I/O vers le FS de Node-L ; Cm (ruches registre) ; console Win32 | `cmd`-like : `type`, `dir`, `reg query` |
| **16 — DLL Win32 de Wine** | kernelbase/kernel32/ucrtbase/advapi32 de Wine branchées sur la ntdll Prism | Des utilitaires console Windows **non modifiés** (outils Sysinternals console, 7-Zip CLI) tournent |
| **17 — Services Windows** | SCM (`services.exe`), ALPC minimal, `svchost`, `sc.exe` | Un service s'installe, démarre, est stoppé, redémarre après crash |
| **18 — Graphique** | win32k minimal (fenêtres/GDI) sur le compositeur Node-L ; ensuite Vulkan + DXVK/VKD3D | Une appli Win32 GUI s'affiche. ⚠️ Pour les **jeux**, le vrai verrou est le **pilote GPU** (amdgpu/Mesa à porter), un chantier aussi gros que tout le NT |
| **19 — Hôte de pilotes `.sys` confiné + attestation Prism** | exports `ntoskrnl`/`hal` pour pilotes génériques, chaque pilote derrière l'IOMMU ; démarrage mesuré (TPM), Secure Boot avec clés Prism, API d'attestation proposée aux éditeurs | Un pilote KMDF simple se charge ; un DMA hors de sa zone est bloqué ; quote TPM vérifiable |

## 5. Ordres de grandeur (honnêtes)

- Phases 12→14 : le cœur du noyau NT, plusieurs milliers de lignes. C'est ce qui
  transforme Node-W de démo en système.
- Phases 15→17 : de quoi lancer des **programmes Windows console réels** et des services.
- Phase 18 : de quoi lancer des **jeux**, à condition d'avoir un pilote GPU. C'est le
  plus gros risque du projet, devant NT.
- Ce que fait Proton aujourd'hui (des milliers de jeux), Prism ne le rattrapera pas avant
  des années. L'intérêt de Prism est ailleurs : l'**isolation** (un `.exe` hostile
  confiné par CR3 + IOMMU, sans hyperviseur) et la **latence** (cœurs dédiés).
