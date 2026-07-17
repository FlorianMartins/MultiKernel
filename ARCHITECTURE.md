# NEXUS-OS — Architecture & Plan de Vol

> **Système d'exploitation asymétrique multi-noyau (Asymmetric Multikernel / Microkernel Coordinator)**
> Partitionnement statique d'une machine x86_64 unique en deux **nœuds souverains** s'exécutant en parallèle sur le même silicium, sans hyperviseur lourd :
> - **Node-L** — sous-système POSIX / Linux (monolithique minimal).
> - **Node-W** — environnement compatible NT/Windows (chargeur PE/COFF + émulation des sous-systèmes Win32/Executive).
>
> Objectif directeur : **accès matériel direct (bare-metal) pour chaque nœud**, condition nécessaire aux logiciels de sécurité et anti-cheat qui refusent de tourner sous virtualisation classique.

**Statut du document :** `DRAFT v0.1` — Session 1, exploration & planification.
**Date :** 2026-07-17

> 🔒 **Sécurité** : la thèse de sécurité (isolation par confinement, TCB minimal) et le
> modèle de menace directeur sont dans [`DOCS/security-model.md`](DOCS/security-model.md)
> — **à relire au début de chaque phase**. Objectif produit : OS léger/performant pour
> **gaming + cybersécurité**, moderne, avec outils Linux optionnels (façon Kali).

---

## 0. Table des matières

1. [Philosophie & positionnement technique](#1-philosophie--positionnement-technique)
2. [Architecture des répertoires](#2-architecture-des-répertoires)
3. [Le Super-Noyau (Coordinator) : partitionnement CPU & mémoire](#3-le-super-noyau-coordinator--partitionnement-cpu--mémoire)
4. [Protocole IPC : mémoire partagée lock-free](#4-protocole-ipc--mémoire-partagée-lock-free)
5. [Modèle de partitionnement matériel (HAL / IOMMU)](#5-modèle-de-partitionnement-matériel-hal--iommu)
6. [Plan de route en 6 phases (testables sous QEMU)](#6-plan-de-route-en-6-phases-testables-sous-qemu)
7. [Risques, angles morts & questions ouvertes](#7-risques-angles-morts--questions-ouvertes)
8. [Glossaire](#8-glossaire)

---

## 1. Philosophie & positionnement technique

### 1.1 Ce que NEXUS-OS *est*

NEXUS-OS est un **coordinateur de multikernel asymétrique**. L'idée centrale — inspirée des travaux académiques **Barrelfish** (multikernel, ETH Zürich), **Popcorn Linux** (replicated-kernel over partitioned cores) et **Fos/Helios (satellite kernels, Microsoft Research)** — est de traiter une machine multi-cœurs comme un **réseau de nœuds** plutôt que comme un seul système à mémoire partagée cohérente.

Le Coordinator est un **microkernel minimaliste** qui, une fois le boot terminé, ne fait presque plus rien : il ne fait *pas* d'ordonnancement des threads des nœuds, ne fait *pas* de pagination pour eux, n'intercepte *pas* leurs syscalls. Il n'est **ni un hyperviseur, ni un OS hôte**. Son rôle est :

1. **Au boot** : découvrir la topologie matérielle, partitionner les cœurs, la RAM et les périphériques, puis démarrer chaque nœud sur *ses* cœurs.
2. **En régime établi** : arbitrer uniquement ce qui *doit* être partagé (horloge de référence, canaux IPC, watchdog, reconfiguration à chaud), et rester sinon totalement en retrait.

### 1.2 Ce que NEXUS-OS *n'est pas*

| N'est pas | Pourquoi c'est important |
|---|---|
| Un hyperviseur type-1 (KVM, Xen, Hyper-V) | Pas de VM-exit sur chaque accès sensible ; pas de EPT/NPT imposé ; l'anti-cheat voit du **vrai matériel**, du **vrai CPUID**, pas d'hint hyperviseur (bit 31 de `CPUID.1:ECX` laissé à 0). |
| Un conteneur / namespace | Isolation *physique* (cœurs + RAM + IOMMU), pas logique. |
| Un dual-boot | Les deux OS tournent **simultanément**, pas l'un après l'autre. |
| Un émulateur (WINE/QEMU-user) | Node-W exécute nativement du code x86_64 sur des cœurs réels ; seul le *chargement* PE et l'ABI kernel sont réimplémentés. |

### 1.3 Le pari « ils s'ignorent superbement »

Le principe de conception dominant est l'**isolation par partitionnement statique** (« share-nothing by default »). À la fin du boot :
- Chaque cœur logique appartient à **exactement un** domaine (`COORD`, `NODE_L`, `NODE_W`).
- Chaque plage de RAM physique est possédée par **exactement un** domaine.
- Chaque périphérique PCIe est routé (via IOMMU) vers **exactement un** domaine (ou vers le Coordinator qui le proxifie).
- Les seuls octets de RAM *réellement* partagés sont les **fenêtres IPC** explicitement mappées, non cohérentes par convention (voir §4).

Tout le reste du design découle de cet invariant.

---

## 2. Architecture des répertoires

Structure modulaire créée à la racine du dépôt. Convention : dossiers de sous-systèmes en **MAJUSCULES**, méta/outillage adapté. Chaque feuille contient un `.gitkeep` tant qu'elle est vide.

```
nexus-os/
├── ARCHITECTURE.md            # CE DOCUMENT — source de vérité du design
├── CLAUDE.md                  # Contraintes projet (stack, workflow)
│
├── BOOT/                      # Chaîne d'amorçage bas niveau
│   ├── grub/                  #   config GRUB2 + Multiboot2 header
│   ├── stage2/                #   stub 32→64 bits, passage long mode, remise du contrôle au Coordinator
│   └── config/                #   partition map, layout mémoire statique, cmdline
│
├── COORDINATOR/               # LE SUPER-NOYAU (microkernel de coordination)
│   ├── core/                  #   init, IDT/GDT globales, gestion des exceptions du domaine COORD
│   ├── smp/                   #   séquence de boot AP (INIT-SIPI-SIPI), assignation des cœurs aux nœuds
│   ├── topology/              #   parsing ACPI (MADT/SRAT/SLIT), graphe cœurs↔NUMA↔caches
│   ├── mm/                    #   partitionnement RAM physique, allocateur de régions (pas de pagination des nœuds)
│   ├── monitor/               #   watchdog, heartbeat IPI, reconfiguration/redémarrage d'un nœud à chaud
│   └── panic/                 #   fault containment : isoler un nœud crashé sans tuer l'autre
│
├── NODE-L/                    # Nœud POSIX / Linux minimal
│   ├── kernel/                #   noyau monolithique compact (init spécifique multikernel)
│   ├── mm/                    #   VM/pagination PROPRE à Node-L (dans sa partition RAM)
│   ├── sched/                 #   ordonnanceur local aux cœurs de Node-L
│   ├── syscall/               #   table d'appels POSIX
│   ├── fs/                    #   VFS + pilotes FS (dont accès stockage via IPC si disque proxifié)
│   └── userland/              #   init, shell, binaires de test POSIX
│
├── NODE-W/                    # Nœud compatible NT / Windows
│   ├── kernel/                #   micro-executive : threads, VM, objets NT (dans sa partition)
│   ├── executive/             #   Object Manager, I/O Manager, gestion des handles NT
│   ├── ntdll/                 #   réimplémentation de la surface ntdll (Nt*/Zw* syscalls)
│   ├── win32/                 #   sous-système Win32 (kernel32/user32 minimal, subset)
│   └── registry/              #   hive registre (backing store pour la compat applicative)
│
├── IPC/                       # Cœur de la communication inter-nœuds
│   ├── ring/                  #   ring buffers SPSC/MPSC lock-free (implémentation + preuve d'ordre mémoire)
│   ├── proto/                 #   définition des messages (schéma binaire versionné, ABI figée)
│   └── doorbell/              #   signalisation par IPI (sonnette) + mode polling adaptatif
│
├── HAL/                       # Hardware Abstraction Layer & pilotes
│   ├── acpi/                  #   tables ACPI (MADT, SRAT, DMAR, FADT)
│   ├── apic/                  #   Local APIC / x2APIC + IO-APIC, routage des IRQ par domaine
│   ├── iommu/                 #   VT-d/AMD-Vi : DMA remapping, isolation périphérique↔domaine
│   ├── pci/                   #   énumération PCIe, attribution exclusive des BDF aux domaines
│   ├── net/                   #   pilote NIC (propriétaire d'un domaine, ou proxifié via IPC)
│   ├── block/                 #   pilote stockage (NVMe/AHCI)
│   ├── serial/                #   UART 16550 — console de debug par domaine (ports séparés)
│   ├── gpu/                   #   passthrough / attribution GPU (stretch)
│   └── input/                 #   clavier/souris — arbitrage de propriété
│
├── LOADERS/                   # Chargeurs de format exécutable
│   ├── elf/                   #   ELF64 (Node-L)
│   ├── pe/                    #   PE/COFF (Node-W) : sections, imports, relocations, TLS
│   └── shims/                 #   couches d'ABI / thunks d'appels système
│
├── LIBS/                      # Bibliothèques partagées du code kernel
│   ├── libkc/                 #   "kernel C" freestanding : mem*, str*, printf minimal, structures
│   └── librt/                 #   primitives runtime : atomics, barrières mémoire, spinlocks (usage interne nœud)
│
├── DOCS/                      # Spécifications (À REMPLIR AVANT DE CODER — cf. CLAUDE.md)
│   ├── specs/                 #   specs formelles par sous-système (ABI IPC, layout mémoire, boot protocol)
│   └── notes/                 #   ADR, journaux de recherche, benchmarks
│
├── TOOLS/                     # Outillage de build & debug
│   ├── image/                 #   construction de l'image disque bootable (image-builder)
│   ├── gdb/                   #   scripts GDB, connexion QEMU multi-cœurs, symboles par domaine
│   └── ci/                    #   pipeline d'intégration (build + tests QEMU headless)
│
└── TESTS/                     # Tests
    ├── unit/                  #   tests unitaires freestanding (mock runner hôte)
    ├── mocks/                 #   doubles de test (faux ACPI, faux ring, faux APIC)
    └── qemu/                  #   scénarios d'intégration bootés sous QEMU + assertions
```

**Justification de la modularité :** la frontière la plus dure du projet est celle entre les trois domaines (`COORD`, `NODE_L`, `NODE_W`). L'arborescence la matérialise : aucun fichier de `NODE-L/` ne doit `#include` un fichier de `NODE-W/`, et réciproquement. Le seul contrat partagé transverse est **`IPC/proto/`** (l'ABI des messages) et **`LIBS/`** (code freestanding sans état global). Cette règle de dépendance est vérifiable mécaniquement en CI (voir Phase 4).

---

## 3. Le Super-Noyau (Coordinator) : partitionnement CPU & mémoire

### 3.1 Vue d'ensemble de la séquence de boot

```
Firmware (UEFI/BIOS)
   │
   ▼
GRUB2 ── Multiboot2 ──►  BOOT/stage2  (BSP en mode 32 bits)
   │                         │  active PAE + long mode, monte une pagination provisoire
   │                         ▼
   │                    COORDINATOR/core  (BSP en 64 bits, seul cœur actif)
   │                         │  1. Parse ACPI (topologie)                [COORDINATOR/topology]
   │                         │  2. Décide le partitionnement             [COORDINATOR/mm + smp]
   │                         │  3. Programme l'IOMMU                     [HAL/iommu]
   │                         │  4. Charge les images Node-L / Node-W en RAM
   │                         │  5. Réveille les AP et les assigne        [COORDINATOR/smp]
   │                         ▼
   │           ┌─────────────┴─────────────┐
   │           ▼                           ▼
   │      Node-L (cœurs 0..k)         Node-W (cœurs k+1..n)
   │      démarre son propre OS       démarre son propre OS
   │      dans SA partition RAM       dans SA partition RAM
   │           └─────────────┬─────────────┘
   │                         ▼
   │              Coordinator se retire → mode "monitor"
   │              (heartbeat, IPC arbitration, hot-restart)
```

Le **BSP (Bootstrap Processor)** est le cœur que le firmware démarre. Le Coordinator ne s'exécute *que* sur le BSP (ou sur un petit cœur dédié `COORD` réservé). Les **AP (Application Processors)** sont endormis en attente de réveil.

### 3.2 Découverte de la topologie (avant tout partitionnement)

Impossible de partitionner ce qu'on ne connaît pas. Le Coordinator lit, dans l'ordre :

| Table ACPI | Fournit | Usage NEXUS-OS |
|---|---|---|
| **MADT** (APIC) | Liste des Local APIC / x2APIC IDs, IO-APICs, ISO | Énumère les cœurs logiques → identifie leurs APIC IDs pour les IPI |
| **SRAT** | Association cœur↔nœud NUMA et plage RAM↔nœud NUMA | Aligne le partitionnement sur la topologie NUMA (un nœud logique ⊆ un nœud NUMA si possible) |
| **SLIT** | Matrice de distances NUMA | Décide quel nœud logique reçoit la RAM la plus proche de ses cœurs |
| **DMAR** (DMAR/VT-d) | Description des IOMMU | Programme l'isolation DMA (§5) |
| **FADT/MCFG** | ECAM PCIe, power mgmt | Énumération PCIe, ACPI shutdown/reset |

Le décodage `x2APIC ID → (package, core, thread)` se fait via `CPUID.0BH/1FH` (extended topology). **Règle d'or NUMA :** on n'attribue jamais à un nœud logique de la RAM d'un autre socket que celui de ses cœurs, sauf pénurie — sinon chaque accès mémoire traverse l'interconnect (QPI/UPI) et détruit les performances.

### 3.3 Partitionnement des cœurs CPU (le « split »)

La configuration du split est **statique et déclarée** (dans `BOOT/config/`), p.ex. :

```
# Exemple sur une machine 16 cœurs / 32 threads, 2 sockets
COORD  : APIC 0                      (1 thread, réservé au monitor)
NODE_L : APIC 1..15   (socket 0)     (POSIX)
NODE_W : APIC 16..31  (socket 1)     (NT-compat, aligné sur son propre socket NUMA)
```

**Mécanique du réveil sélectif (INIT–SIPI–SIPI) :**

1. Le BSP construit, pour chaque AP, un **trampoline** distinct (code de démarrage 16→64 bits placé sous 1 Mio) qui pointe vers *l'entrée du bon nœud* et *son propre CR3/pile*.
2. Le BSP envoie la séquence **INIT IPI → SIPI → SIPI** via le Local APIC (registre `ICR`), en ciblant l'APIC ID de l'AP visé. C'est le protocole standard MP Intel de réveil d'un AP.
3. L'AP démarre au trampoline, bascule en long mode, charge **le CR3 de sa partition** (donc voit uniquement *sa* RAM), installe **sa propre IDT locale**, puis saute dans le noyau de son nœud.
4. **Point clé de l'isolation :** une fois réveillé, un AP de Node-W n'a **aucune raison ni aucun moyen** de toucher Node-L : sa pagination ne mappe pas la RAM de Node-L, et son domaine IOMMU ne route pas les périphériques de Node-L.

**« S'ignorer superbement » — comment on l'obtient concrètement :**

| Mécanisme | Effet |
|---|---|
| **CR3 par domaine** | Chaque nœud a son arbre de pages ; la RAM de l'autre n'est simplement pas mappée → invisible, inatteignable même par bug de pointeur. |
| **IDT/APIC locale par cœur** | Les interruptions d'un cœur Node-L ne sont jamais délivrées à un cœur Node-W. Le routage IO-APIC/MSI envoie chaque IRQ de périphérique vers l'APIC ID du domaine propriétaire. |
| **Pas d'IPI croisé non sollicité** | Le seul IPI autorisé entre domaines est la « sonnette » IPC sur un vecteur réservé et convenu (§4.4). Tout autre vecteur inter-domaine est traité comme une faute. |
| **TSC/horloge** | On utilise le TSC (invariant) comme temps monotone commun de référence ; chaque nœud gère *son* timer local (APIC timer) sans toucher celui de l'autre. |

### 3.4 Partitionnement de la RAM

Le Coordinator construit une **carte de propriété physique** (physical ownership map) à partir de la mémoire disponible (Multiboot2 memory map ∩ SRAT) :

```
┌────────────────────────────────────────────────────────────────┐
│ 0                            RAM PHYSIQUE                    MAX │
├──────────┬───────────────────────┬───────────────────┬─────────┤
│ COORD    │       NODE_L           │      NODE_W        │ IPC SHM │
│ (petit)  │  (pagination propre)   │ (pagination propre)│ (partagé│
│          │                        │                   │ mappé 2×)│
└──────────┴───────────────────────┴───────────────────┴─────────┘
```

- **Région COORD** : minuscule (quelques Mio) — code+données du microkernel + la ownership map.
- **Région NODE_L / NODE_W** : chaque nœud est *propriétaire exclusif* de sa plage ; il y fait *sa* pagination interne comme un OS normal. Le Coordinator ne page **jamais** pour eux après le boot.
- **Région IPC SHM** : la *seule* zone volontairement mappée dans **les deux** arbres de pages (dans Node-L *et* Node-W), avec des attributs de cache contrôlés (voir §4.2). C'est le canal.

**Invariant vérifié au boot :** l'union des régions est disjointe (∩ = ∅) sauf la fenêtre IPC ; toute RAM non attribuée est marquée réservée (jamais donnée à un nœud par accident). Cet invariant sera une **assertion testée** dès la Phase 1.

### 3.5 Le Coordinator après le boot : quasi-inexistant

Une fois les nœuds lancés, le Coordinator entre en **mode monitor** (`COORDINATOR/monitor`) :
- **Heartbeat** : chaque nœud incrémente périodiquement un compteur dans une zone lue par le Coordinator ; absence de progression = nœud figé.
- **Fault containment** (`COORDINATOR/panic`) : si Node-W triple-fault, le Coordinator le détecte (via un handler installé pour le domaine ou l'absence de heartbeat), **gèle ses cœurs** (NMI/INIT), marque sa RAM comme suspecte, notifie Node-L via IPC, et peut le **redémarrer à chaud** sans perturber Node-L.
- **Reconfiguration** (stretch) : rendre/retirer un cœur à un nœud à chaud (offline d'AP + réassignation).

---

## 4. Protocole IPC : mémoire partagée lock-free

### 4.1 Contraintes de conception

Le canal relie deux OS **qui ne partagent ni ordonnanceur, ni allocateur, ni horloge d'interruption**, et qui peuvent tourner à des fréquences différentes. Il doit :
- être **sans verrou** (un nœud figé ne doit jamais bloquer l'autre — pas de mutex partagé, un titulaire de lock mort = deadlock inter-OS fatal) ;
- **survivre au crash de l'autre bout** (bornes validées, jamais de déréférencement de pointeur venu de l'autre nœud) ;
- avoir une **ABI binaire figée et versionnée** (les deux nœuds sont compilés séparément, potentiellement par des toolchains différentes) ;
- **ne dépendre d'aucune cohérence de cache implicite au-delà de ce que x86 garantit** (voir 4.2).

### 4.2 Fenêtre de mémoire partagée : modèle mémoire

La région IPC SHM est mappée dans les deux nœuds. Choix des attributs de page :

- **Option A (retenue au départ) — WB (Write-Back) + barrières explicites.** Sur x86_64, la cohérence de cache matérielle (MESI) est *garantie entre cœurs* même entre domaines : deux cœurs qui mappent la même ligne physique la voient cohérente. On s'appuie dessus, **mais** l'ordre de visibilité exige des barrières : le modèle x86 est TSO (Total Store Order), qui autorise le *store-load reordering*. On insère donc `sfence`/`mfence` (ou des accès `atomic` avec `release`/`acquire`) aux points de publication d'indices (voir 4.3).
- **Option B (fallback debug) — UC/WC.** Non-caché : plus lent mais sémantique d'ordre plus simple, utile pour bringup.

> Décision consignée dans un futur ADR `DOCS/notes/` : **WB + acquire/release** en production, **UC** disponible en flag de debug.

### 4.3 Ring buffer lock-free SPSC (brique de base)

Un canal unidirectionnel = un **ring buffer à producteur unique / consommateur unique (SPSC)**. C'est le cas lock-free le plus robuste : avec exactement un producteur et un consommateur, **aucune opération atomique de type CAS n'est requise** — seuls des stores/loads d'indices avec barrières.

```
        head (écrit par le PRODUCTEUR)         tail (écrit par le CONSOMMATEUR)
          │                                      │
          ▼                                      ▼
   ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐   buffer de N slots
   │ .. │ ✔  │ ✔  │ ✔  │    │    │    │    │    │ .. │   (N = puissance de 2)
   └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘
        └── données valides ──┘
```

**En-tête de canal (dans la SHM, ABI figée) :**
```
struct nexus_ring_hdr {
    u32 magic;          // 'NXR0' — validation + versionnage
    u32 version;        // version de l'ABI proto
    u32 slot_size;      // taille d'un slot (fixe)
    u32 slot_count;     // N (puissance de 2)
    u64 producer_head;  // ligne de cache A — écrit par le producteur SEUL
    u8  _pad0[64 - 8];  // padding pour éviter le FALSE SHARING
    u64 consumer_tail;  // ligne de cache B — écrit par le consommateur SEUL
    u8  _pad1[64 - 8];
};
// suivi de slot_count * slot_size octets de données
```

**Point critique — false sharing :** `producer_head` et `consumer_tail` sont **sur des lignes de cache distinctes** (padding 64 o). Sinon, chaque mise à jour d'indice par un nœud invaliderait la ligne dans le cache de l'autre nœud (ping-pong MESI), détruisant le débit. C'est *la* leçon de performance des ring buffers cross-core (cf. LMAX Disruptor).

**Protocole d'écriture (producteur) :**
1. Lire `consumer_tail` (`acquire`). Si `head - tail == N` → plein, on n'écrit pas (retourne « busy », **jamais de blocage**).
2. Écrire la charge utile dans `slot[head % N]`.
3. **Barrière `release`** (garantit que les données du slot sont visibles *avant* la publication de l'indice).
4. Publier `producer_head = head + 1`.

**Protocole de lecture (consommateur) :**
1. Lire `producer_head` (`acquire`). Si `head == tail` → vide.
2. **Barrière `acquire`** puis lire le slot `slot[tail % N]`.
3. Publier `consumer_tail = tail + 1` (`release`).

L'appariement **release (producteur) ↔ acquire (consommateur)** est ce qui rend l'échange correct sur x86 TSO sans lock. Le consommateur ne voit jamais un indice avancé sans voir aussi les données correspondantes.

### 4.4 Signalisation : sonnette (doorbell) + polling adaptatif

Un ring lock-free dit *quoi* échanger mais pas *quand réveiller* l'autre nœud. Deux modes, combinés :

- **Polling** (basse latence, coûte du CPU) : le consommateur lit `producer_head` en boucle. Idéal pour les cœurs dédiés I/O à haut débit.
- **Doorbell par IPI** (`IPC/doorbell`) : après avoir publié un message, le producteur envoie un **IPI sur un vecteur réservé** à un cœur du domaine cible ; le handler de ce vecteur réveille/notifie le consommateur. C'est le *seul* IPI inter-domaine légitime.
- **Hybride adaptatif** : polling actif tant que le trafic est soutenu (évite le coût de l'IPI), bascule en doorbell après un seuil d'inactivité (évite de brûler du CPU à vide). Politique inspirée de NAPI (Linux) et du busy-poll/IPI des transports virtio.

### 4.5 Ce qui transite : I/O réseau & stockage

Node-W (ou Node-L) peut ne pas posséder physiquement une NIC ou un disque : le domaine **propriétaire** du périphérique agit en **back-end**, l'autre en **front-end**, exactement comme un split-driver virtio (mais entre deux OS bare-metal, pas VM↔hyperviseur) :

```
  NODE-W (front-end réseau)                      NODE-L (back-end, possède la NIC)
  ┌──────────────────────┐   ring TX (SPSC)      ┌───────────────────────────────┐
  │ pilote virtuel "nexnet"│ ───────────────────► │ démux → pilote NIC réel (HAL/net)│ → câble
  │                        │ ◄─────────────────── │ RX réel → ring RX (SPSC)         │
  └──────────────────────┘   ring RX (SPSC)      └───────────────────────────────┘
```

- **Deux rings unidirectionnels** par canal (TX/RX) → conserve la simplicité SPSC.
- **Zéro-copie possible** : les buffers de données vivent dans la SHM ; on ne transmet que des **descripteurs** (offset+longueur dans la fenêtre partagée), jamais des pointeurs propres à un nœud.
- **Sécurité :** le back-end **valide toujours** offset/longueur contre les bornes de la fenêtre SHM avant tout accès — un front-end compromis ne doit pas pouvoir faire lire hors zone au back-end (défense indispensable vu le contexte sécurité/anti-cheat).

### 4.6 Résumé des garanties IPC

| Propriété | Comment |
|---|---|
| Lock-free | SPSC, indices simples, aucun CAS, aucun mutex partagé |
| Non-bloquant | file pleine/vide → retour immédiat « busy/empty », jamais d'attente sur l'autre nœud |
| Tolérant au crash | bornes validées, magic/version vérifiés, jamais de pointeur étranger déréférencé |
| Débit | pas de false sharing (padding 64 o), zéro-copie par descripteurs |
| Correct sur x86 | appariement acquire/release, barrières aux points de publication |
| Évolutif | ABI `magic`+`version` figée dans `IPC/proto/` |

---

## 5. Modèle de partitionnement matériel (HAL / IOMMU)

L'accès direct au matériel étant *l'exigence fondatrice*, le partitionnement des périphériques est aussi critique que celui des cœurs.

- **Attribution PCIe exclusive (`HAL/pci`)** : chaque fonction PCIe (BDF = Bus/Device/Function) est assignée à un seul domaine. Le Coordinator énumère l'espace de config (ECAM/MCFG) au boot et distribue les périphériques selon `BOOT/config`.
- **Isolation DMA par IOMMU (`HAL/iommu`)** : c'est **le** point non négociable. Sans IOMMU, un périphérique attribué à Node-W pourrait, via DMA, lire/écrire la RAM de Node-L (le DMA ignore la pagination CPU). On programme donc **VT-d / AMD-Vi** pour que chaque périphérique ne puisse adresser en DMA **que** la partition RAM de son domaine propriétaire (+ la fenêtre IPC si nécessaire). Table DMAR → domaines IOMMU distincts par nœud.
- **Routage des interruptions (`HAL/apic`)** : IO-APIC et MSI/MSI-X programmés pour délivrer l'IRQ de chaque périphérique à un APIC ID appartenant au domaine propriétaire. Interrupt remapping activé (empêche un périphérique d'injecter une interruption dans le mauvais domaine).
- **Consoles séparées (`HAL/serial`)** : chaque domaine possède son propre port UART (ou un multiplexage convenu) pour un debug indépendant sous QEMU — essentiel pour lire les deux nœuds en parallèle.
- **Périphériques non partageables** (GPU `HAL/gpu`, input `HAL/input`) : attribution exclusive au départ ; arbitrage/commutation = stretch goal.

---

## 6. Plan de route en 6 phases (testables sous QEMU)

Chaque phase est **incrémentale, bootable et vérifiable**. Convention de test : QEMU x86_64 (`-smp N`), consoles série capturées, assertions automatiques dans `TESTS/qemu`. GDB stub QEMU (`-s -S`) pour l'inspection multi-cœurs (`TOOLS/gdb`).

> ⚠️ **Note QEMU/IOMMU** : l'IOMMU virtuelle de QEMU (`intel-iommu`) est limitée. Les phases matérielles fines (DMA remapping réel) seront validées d'abord *fonctionnellement* sous QEMU, puis sur **vrai matériel** en fin de parcours. Cette limite est explicitement tracée (pas de fausse confiance).

### Phase 1 — Coordinator monocœur & découverte topologie
**But :** booter via GRUB/Multiboot2, passer en long mode, faire tourner le Coordinator sur le seul BSP, parser l'ACPI et **imprimer** la topologie + le plan de partitionnement (sans encore réveiller d'AP).
**Livrables :** `BOOT/`, `COORDINATOR/core`, `COORDINATOR/topology`, `HAL/acpi`, `HAL/serial`, `LIBS/libkc`, `TOOLS/image`.
**Vérification QEMU :**
- `qemu-system-x86_64 -smp 4 -serial stdio` boote sans triple-fault.
- La console série affiche : nombre de cœurs détectés, APIC IDs, plan `COORD/NODE_L/NODE_W`, carte RAM.
- **Assertion :** l'union des régions RAM planifiées est disjointe (test dans `TESTS/qemu`).

### Phase 2 — Réveil sélectif des AP & partitionnement effectif
**But :** implémenter INIT-SIPI-SIPI, réveiller les AP, assigner chaque cœur à un domaine, chaque AP charge **son** CR3 (voit uniquement sa partition RAM) et exécute une boucle de preuve d'isolation.
**Livrables :** `COORDINATOR/smp`, `COORDINATOR/mm`, trampolines, pagination par domaine.
**Vérification QEMU :**
- Chaque AP imprime « je suis le cœur X, domaine D, je vois \[base..limite\] Mio ».
- **Test négatif d'isolation :** un cœur Node-W tente d'accéder à une adresse de la partition Node-L → **#PF** capturé et journalisé (prouve que la RAM de l'autre n'est pas mappée).
- Compteur : `nb_AP_réveillés == nb_cœurs_planifiés - COORD`.

### Phase 3 — Canal IPC lock-free bout-à-bout
**But :** établir la fenêtre SHM, implémenter le ring SPSC + doorbell, faire dialoguer deux « nœuds factices » (stubs, pas encore les vrais OS) : l'un produit, l'autre consomme.
**Livrables :** `IPC/ring`, `IPC/proto`, `IPC/doorbell`, `LIBS/librt` (atomics/barrières), `TESTS/unit` (ring testé en isolation sur l'hôte).
**Vérification :**
- **Unitaire (hôte) :** stress-test SPSC multi-thread (producteur/consommateur), 10⁷ messages, **0 perte, 0 corruption, ordre FIFO** ; détecteur de false sharing (comparaison de débit avec/sans padding).
- **Intégration QEMU :** cœur A envoie séquence 1..1000 → cœur B les reçoit **dans l'ordre**, publie un checksum sur sa console ; doorbell IPI vérifié (le consommateur en `hlt` est réveillé).
- **Test de robustesse :** on « fige » le consommateur → le producteur passe « busy » sans jamais se bloquer.

### Phase 4 — Node-L : premier OS souverain (POSIX minimal)
**But :** faire booter Node-L sur ses cœurs, dans sa partition, avec pagination propre, un ordonnanceur local, quelques syscalls POSIX, un init + shell en userland, et un pilote back-end (ex. block) exposé via IPC.
**Livrables :** `NODE-L/*`, `LOADERS/elf`, `HAL/block` ou `HAL/net`, règle de dépendance CI (aucun `#include` croisé NODE-L↔NODE-W).
**Vérification QEMU :**
- Node-L boote pendant que le Coordinator reste en mode monitor ; shell POSIX interactif sur sa console série.
- Un programme userland ELF s'exécute (test d'appels système).
- **CI d'architecture :** script `TOOLS/ci` échoue si un fichier `NODE-L/` référence `NODE-W/` (et inverse).
- Heartbeat de Node-L visible côté Coordinator.

### Phase 5 — Node-W : chargeur PE & surface NT minimale
**But :** faire booter Node-W en parallèle de Node-L, charger un exécutable **PE/COFF** trivial (relocations, imports, TLS), et servir un sous-ensemble d'appels `Nt*`/Win32 suffisant pour un « hello world » natif. Node-W consomme le réseau/stockage via IPC (front-end) servi par Node-L (back-end).
**Livrables :** `NODE-W/*`, `LOADERS/pe`, `LOADERS/shims`, canal virtio-like réseau/bloc complet.
**Vérification QEMU :**
- **Les deux OS tournent simultanément** : deux consoles série actives en parallèle.
- Un `.exe` PE minimal se charge et s'exécute sur les cœurs Node-W (log des Nt* appelés).
- **Test I/O inter-nœud :** Node-W écrit un bloc / envoie un ping réseau → transite par IPC → traité par le back-end Node-L → réponse renvoyée. Assertion sur le contenu.
- **Test d'isolation renforcé :** crash volontaire d'un `.exe` Node-W → seul Node-W est affecté, Node-L continue (heartbeat L intact).

### Phase 6 — Résilience, matériel réel & durcissement
**But :** passer du « ça boote sous QEMU » au « ça isole vraiment » : fault-containment complet (redémarrage à chaud d'un nœud), IOMMU réelle, validation sur matériel physique, et durcissement sécurité de la surface IPC (le contexte anti-cheat exige des frontières infalsifiables).
**Livrables :** `COORDINATOR/monitor`, `COORDINATOR/panic`, `HAL/iommu`, `HAL/pci` (attribution exclusive réelle), `HAL/apic` (interrupt remapping), suite de tests `TESTS/qemu` + banc matériel.
**Vérification :**
- **Fault containment :** on force un triple-fault de Node-W → le Coordinator le gèle, notifie Node-L, **redémarre Node-W à chaud**, sans que Node-L ait bronché (heartbeat continu).
- **IOMMU (QEMU `intel-iommu` puis matériel) :** un périphérique attribué à Node-W ne peut PAS faire de DMA vers la RAM de Node-L (tentative → bloquée/journalisée par l'IOMMU).
- **Anti-cheat readiness (checklist) :** `CPUID` hyperviseur bit à 0, TSC natif, accès direct MSR/IO du domaine confirmé, pas de VM-exit sur instructions sensibles.
- **Matériel réel :** boot sur au moins une machine physique 2-sockets ; les deux nœuds vivent en parallèle ; mesures de latence/débit IPC réelles consignées dans `DOCS/notes`.

### Récapitulatif des jalons

| Phase | Cœur livré | Question à laquelle on répond « oui » |
|---|---|---|
| 1 | Coordinator + topologie | « Sait-on découvrir et planifier le partitionnement ? » |
| 2 | Split CPU+RAM effectif | « Les cœurs s'ignorent-ils vraiment (isolation RAM prouvée) ? » |
| 3 | IPC lock-free | « Peuvent-ils communiquer sans se bloquer ni se corrompre ? » |
| 4 | Node-L souverain | « Un vrai OS POSIX tourne-t-il dans sa partition ? » |
| 5 | Node-W + PE + I/O croisée | « Deux OS hétérogènes tournent-ils ensemble et échangent-ils des I/O ? » |
| 6 | Résilience + matériel réel | « L'isolation tient-elle sous crash, DMA et sur du vrai silicium ? » |

---

## 7. Risques, angles morts & questions ouvertes

Points à trancher **avant** d'attaquer le code (candidats pour `DOCS/adr` — Architecture Decision Records) :

1. **Node-W est le risque N°1.** Réimplémenter une surface NT/Win32 même minimale est un chantier colossal et un terrain juridique/technique glissant (compat anti-cheat réelle ≠ « hello world PE »). *Question :* jusqu'où va-t-on ? Cible réaliste = charger et exécuter un PE console natif ; la compat applicative anti-cheat complète est un horizon, pas un livrable de v1.
2. **IOMMU sous QEMU** : partiellement émulée → la vraie garantie DMA ne sera prouvée que sur matériel (Phase 6). Ne pas surestimer le vert en CI.
3. **Cohérence de cache inter-domaine** : on s'appuie sur MESI garanti par x86 sur la SHM WB. À confirmer expérimentalement (test de la Phase 3) plutôt que par pure confiance.
4. **Horloge & TSC** : suppose un TSC invariant et synchronisé entre sockets. Sur certaines plateformes multi-sockets, le TSC peut désync → prévoir une resynchronisation de référence.
5. **Firmware & handoff** : UEFI moderne complique le retour au « bare-metal pur » (SMM, runtime services). À cadrer en Phase 1.
6. **Anti-cheat detection** : certains anti-cheats détectent *toute* configuration non standard (cœurs manquants, tables ACPI trafiquées). « Pas d'hyperviseur » ne suffit pas forcément ; à étudier honnêtement.
7. **Périphériques réellement partagés** (un seul GPU, un seul clavier) : le passthrough exclusif est simple, le *partage* dynamique est un gros morceau reporté en stretch.

---

## 8. Glossaire

| Terme | Définition |
|---|---|
| **Multikernel** | Modèle OS traitant une machine multi-cœurs comme un réseau de nœuds à mémoire non partagée par défaut (cf. Barrelfish). |
| **Coordinator** | Le microkernel NEXUS-OS qui partitionne au boot puis se retire ; ni hyperviseur ni OS hôte. |
| **Node-L / Node-W** | Les deux OS souverains (POSIX / NT-compat) tournant chacun sur ses cœurs et sa RAM. |
| **BSP / AP** | Bootstrap Processor (cœur de démarrage) / Application Processors (cœurs réveillés ensuite). |
| **INIT-SIPI-SIPI** | Séquence d'IPI standard Intel MP pour réveiller un AP. |
| **IPI** | Inter-Processor Interrupt — interruption d'un cœur vers un autre (via Local APIC). |
| **APIC / x2APIC** | Contrôleur d'interruptions local/IO ; x2APIC = mode étendu (registres MSR, plus d'IDs). |
| **IOMMU (VT-d / AMD-Vi)** | Unité de remapping mémoire pour le DMA des périphériques ; clé de l'isolation matérielle. |
| **SHM** | Shared Memory — la fenêtre RAM volontairement mappée dans les deux nœuds pour l'IPC. |
| **SPSC / MPSC** | Single-Producer-Single-Consumer / Multi-Producer — topologies de file lock-free. |
| **False sharing** | Deux variables sur la même ligne de cache modifiées par deux cœurs → ping-pong MESI, perte de perf. |
| **TSO** | Total Store Order — le modèle mémoire x86 (autorise le store-load reordering, d'où les barrières). |
| **PE/COFF / ELF** | Formats exécutables Windows / Unix respectivement. |
| **ACPI (MADT/SRAT/SLIT/DMAR)** | Tables firmware décrivant CPU, NUMA, distances, et IOMMU. |
| **ADR** | Architecture Decision Record — note actant une décision de conception et sa justification. |

---

*Fin du plan de vol v0.1 — en attente de validation avant le passage au code.*
