# NEXUS-OS — Modèle de menace & principes de sécurité directeurs

> **Statut : document vivant, à relire au début de CHAQUE phase.**
> Version 0.1 — 2026-07-17.
>
> Règle d'or : « le plus sécurisé » **se démontre, ne se décrète pas** (audits, modèle de
> menace écrit, red-team). Ce fichier est la référence ; chaque phase doit dire quels
> contrôles elle ajoute et comment ils sont *vérifiés*.

## 1. La thèse de sécurité (honnête)

NEXUS-OS ne prétend **pas** être « plus sûr que Linux/Windows sur tous les axes » : leur
maturité, leurs années de durcissement et de mitigations exploit sont un mur. La
revendication défendable et différenciante est :

> **Résistance structurelle à la compromission totale : une faille ne donne pas la
> machine entière.** Sécurité *par confinement/compartimentation*, pas *par perfection
> du code*.

C'est un modèle adapté à l'usage **cybersécurité** (lab, red-team, exécution de code
hostile confiné) et cohérent avec l'objectif **gaming** (accès matériel direct, anti-cheat)
sans la couche hyperviseur.

## 2. L'atout architectural (où l'on gagne vraiment)

- **Isolation matérielle par partitionnement, pas logique.** Frontière *physique* :
  CR3 par domaine (déjà prouvé Phase 2 : accès croisé → #PF) + IOMMU à venir. Ce n'est
  pas un namespace ni un ring contournable par un bug de driver. Compromettre un nœud ne
  donne pas l'autre. (Linux/Windows = un seul noyau monolithique géant : le compromettre
  donne tout.)
- **TCB minuscule.** Le Coordinator ne fait presque rien après le boot (déterministe,
  share-nothing). Sécurité ∝ 1/taille du TCB : quelques milliers de lignes sont
  *auditables* ; 30 M de lignes ne le sont pas (argument seL4).
- **Pas d'hyperviseur à attaquer** tout en gardant l'isolation : une couche exposée par
  les autres est ici supprimée.

## 3. Contrôles INDISPENSABLES (sinon la thèse est fausse)

| # | Contrôle | Pourquoi | Statut |
|---|----------|----------|--------|
| C1 | **IOMMU (VT-d/AMD-Vi)** : DMA d'un périphérique borné à la RAM de son domaine | Sans elle, le DMA contourne le partitionnement CPU → isolation nulle | ⏳ Phase 6 (non prouvable sous QEMU, test = matériel réel) |
| C2 | **IPC borné/validé** : tout octet venant de l'autre nœud est hostile par défaut | L'IPC est la NOUVELLE surface d'attaque ; un pointeur cross-domaine déréférencé annule tout | 🟢 Phase 3+5 : `len ≤ PAYLOAD_MAX`, back-end I/O borne `lba/len` (fail-closed), on ne transporte que des octets copiés (jamais de pointeur cross-domaine) |
| C3 | **Boot vérifié / chaîne de confiance** (Secure/Measured Boot, pas d'exécution en place non signée) | GRUB/firmware/SMM restent un maillon | ⏳ à cadrer (Phase 6) |
| C4 | **W^X (Write XOR eXecute)** : pages code RX (NX sur data), pas de RWX | Empêche l'injection/exécution de code | 🔴 **dette actuelle** : le mapping identité 2 MiB est RWX (warning ld). À corriger (NX + attributs par section) |
| C5 | **Confinement de Node-W** (compat NT = grosse surface = talon d'Achille) | Faire tourner du PE/Windows réintroduit du risque ; l'archi le confine SI C1+C2 tiennent | 🟢 Phase 5 **démontré** : un `.exe` Node-W qui touche la RAM Node-L prend un #PF (`run_phase5_crash.sh`) → Node-L survit. Confinement CPU/pagination prouvé (reste C1 IOMMU pour le DMA) |

## 4. Axes ajoutés (angles à ne pas oublier)

- **Side-channels inter-domaines (Spectre/Meltdown/cache/SMT).** Si Node-L et Node-W
  partagent un cache L3 ou des **threads SMT du même cœur physique**, fuite possible
  malgré le partitionnement RAM. → Règle de placement : **ne jamais mettre deux domaines
  sur des threads frères (hyperthreads) du même cœur** ; à terme cache partitioning (Intel
  CAT). ⚠️ Le split actuel (`mm_domain_of`, par index APIC) ne décode pas encore la
  topologie cœur/thread (CPUID.0BH/1FH) — **TODO Phase 2.5**.
- **DoS inter-nœud.** Un nœud qui boucle ne doit pas geler l'autre : garanti
  structurellement (ordonnanceurs séparés, share-nothing, IPC non bloquant → déjà le cas).
- **TCB mesuré et figé.** Viser *petit ET vérifiable*, pas juste petit : suivre les LOC du
  Coordinator, geler sa surface, viser l'audit/preuve. (Idée : `make tcb-count`.)
- **Reproductibilité / supply chain.** Builds déterministes, dépendances épinglées
  (toolchain, GRUB), artefacts vérifiables.
- **Moindre privilège matériel.** Chaque domaine ne reçoit que ses périphériques (attribution
  PCIe exclusive) ; MMIO mappée UC et au strict nécessaire (ex. LAPIC per-CPU en Phase 3).

## 5. Règles de codage sécurité (applicables dès maintenant)

1. **Frontière = méfiance.** Toute donnée franchissant une frontière de domaine (IPC,
   périphérique, entrée utilisateur) est validée/bornée **avant** usage. Jamais de pointeur
   d'un autre domaine déréférencé ; on n'échange que des descripteurs (offset+len) validés
   contre les bornes de la fenêtre partagée.
2. **Fail-closed.** En cas de doute (message malformé, version ABI inconnue, checksum),
   on rejette et on journalise — on ne « devine » pas.
3. **Pas de confiance dans le contenu partagé.** La SHM est lisible par l'autre nœud :
   n'y stocker aucun secret, ne jamais y placer de pointeur de code.
4. **Déterminisme.** Éviter le code dépendant d'état non maîtrisé dans le TCB.

## 6. Processus

- **Écrire/mettre à jour ce modèle de menace au début de chaque phase** (quels actifs,
  quelles frontières, quel attaquant, quels contrôles ajoutés, comment vérifiés).
- Red-team/audit comme livrable, pas comme intention : chaque contrôle a un test
  (unitaire, QEMU, ou banc matériel pour C1/C3).
