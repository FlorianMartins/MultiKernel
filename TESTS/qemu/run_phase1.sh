#!/usr/bin/env bash
# NEXUS-OS — validation automatisée de la Phase 1 sous QEMU.
# Boote l'ISO, capture la console série, vérifie les invariants attendus.
set -u

SMP="${SMP:-4}"
MEM="${MEM:-512}"
ISO="${ISO:-build/nexus-os.iso}"
LOG="$(mktemp)"
TIMEOUT="${TIMEOUT:-20}"

if [ ! -f "$ISO" ]; then
    echo "FAIL: ISO introuvable ($ISO) — lance d'abord 'make iso'"
    exit 2
fi

echo "== Boot QEMU (smp=$SMP, mem=${MEM}M, timeout=${TIMEOUT}s) =="
# Le noyau s'arrête (cli;hlt) en fin de Phase 1 : on borne l'exécution par timeout.
timeout "$TIMEOUT" qemu-system-x86_64 \
    -cdrom "$ISO" -smp "$SMP" -m "$MEM" \
    -serial stdio -display none -no-reboot >"$LOG" 2>&1

echo "----- console série -----"
cat "$LOG"
echo "-------------------------"

fail=0
check() { # motif  libellé
    if grep -qE "$1" "$LOG"; then
        echo "PASS: $2"
    else
        echo "FAIL: $2  (motif manquant: /$1/)"
        fail=1
    fi
}

check "multiboot2 magic OK"                       "handoff Multiboot2"
check "RSDP OK"                                    "RSDP ACPI validé"
check "MADT found"                                 "MADT localisée"
check "enabled: ${SMP}\b"                          "cœurs activés == ${SMP}"
check "ASSERT regions disjoint & sorted: PASS"     "invariant RAM disjointe"
check "Phase 1 complete"                           "fin de Phase 1 atteinte"

# Aucune erreur fatale ne doit apparaître.
if grep -qE "FATAL" "$LOG"; then
    echo "FAIL: message FATAL détecté"
    fail=1
fi

rm -f "$LOG"
if [ "$fail" -eq 0 ]; then
    echo "== RESULT: PHASE 1 OK =="
    exit 0
else
    echo "== RESULT: PHASE 1 ÉCHEC =="
    exit 1
fi
