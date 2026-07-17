# SPEC — Phase 3 : Canal IPC lock-free bout-à-bout

> Statut : `IMPLEMENTED (bringup)`. Portée : ring buffer SPSC sans verrou en mémoire
> partagée + doorbell (IPI), dialogue entre un cœur Node-L (producteur) et un cœur
> Node-W (consommateur), à travers la frontière de domaine.

## 1. Mémoire partagée

La région `[0, 32 MiB)` est déjà mappée à l'identique dans les CR3 Node-L **et** Node-W
(Phase 2). Le ring y réside (BSS du noyau) : producteur et consommateur y accèdent à la
même adresse physique. Cohérence assurée par MESI (x86, mapping WB) + barrières
acquire/release aux points de publication (modèle Option A de `ARCHITECTURE.md` §4.2).

## 2. Ring SPSC lock-free (`IPC/ring`)

- Producteur unique / consommateur unique → aucun CAS, seulement load/store d'indices.
- `producer_head` et `consumer_tail` sur des **lignes de cache séparées** (`_Alignas(64)`)
  → pas de false sharing.
- `push` : plein → renvoie `false` (jamais bloquant). `pop` : vide → `false`.
- Appariement : `push` publie `producer_head` en **release** ↔ `pop` le lit en **acquire**
  (et réciproquement pour `consumer_tail`). Arithmétique d'indices non bornée (wrap u32),
  `slot_count` puissance de 2.
- ABI figée (`IPC/proto`) : `magic 'NXR0'`, `version`, `slot_size=64`, `slot_count`.

## 3. Doorbell (`IPC/doorbell`)

IPI *fixed* (vecteur `0x41`) envoyé via LAPIC ICR. La MMIO LAPIC est désormais mappée
(UC) dans chaque CR3 domaine (PDPT[3]→PD dédié). Le consommateur, une fois sa réception
polling terminée, se met en `sti; hlt` ; le producteur sonne → le handler (EOI + flag)
réveille le consommateur du `hlt` (démonstration du chemin doorbell, sans perte de wakeup
car IF=0 latch l'IPI dans l'IRR).

## 4. Rôles (intégration QEMU)

Le BSP attribue : 1er cœur Node-L activé → **PRODUCER**, 1er cœur Node-W → **CONSUMER**
(si les deux existent, sinon IPC désactivé) ; les autres cœurs gardent le **test
d'isolation** de la Phase 2. Tout se déroule donc en parallèle : isolation + IPC.

## 5. Vérification

- **Unitaire hôte** (`TESTS/unit/test_ring.c`, `make test-unit`) : 2 threads pthread,
  **10⁷ messages**, assertions *0 perte, ordre FIFO strict, checksum exact*, + débit.
- **Intégration QEMU** (`run_phase3.sh`) : producteur envoie `IPC_MSG_COUNT` messages,
  consommateur vérifie ordre + checksum et l'annonce ; doorbell reçu (réveil du `hlt`) ;
  robustesse : si le ring se remplit, le producteur compte des *busy-waits* sans se bloquer.
- Le noyau imprime ses propres verdicts `ASSERT ... : PASS` ; les scripts les grep +
  refusent tout `FAIL/FATAL/HANG`.

## 6. Constantes

`IPC_SLOT_SIZE=64`, `IPC_RING_SLOTS=1024`, `IPC_MSG_COUNT=100000` (QEMU),
`10⁷` (hôte). Doorbell vecteur `0x41`. LAPIC `0xFEE00000`.
