#!/usr/bin/env bash
# NEXUS-OS — validation automatisée de la Phase 2 sous QEMU.
# Vérifie Phase 1 + réveil des AP + preuve d'isolation (#PF croisé).
# QEMU sort tout seul via isa-debug-exit ; un timeout atteint = HANG = échec.
set -u

SMP="${SMP:-4}"
MEM="${MEM:-512}"
ISO="${ISO:-build/nexus-os.iso}"
LOG="${LOG:-$(mktemp)}"
TIMEOUT="${TIMEOUT:-30}"
QUIET="${QUIET:-0}"

if [ ! -f "$ISO" ]; then
    echo "FAIL: ISO introuvable ($ISO) — lance d'abord 'make iso'"
    exit 2
fi

EXPECTED=$((SMP - 1))   # tous les cœurs sauf le BSP (cpu[0]=COORD)

[ "$QUIET" = 0 ] && echo "== Boot QEMU (smp=$SMP, mem=${MEM}M, expected AP=$EXPECTED, timeout=${TIMEOUT}s) =="
timeout "$TIMEOUT" qemu-system-x86_64 \
    -cdrom "$ISO" -smp "$SMP" -m "$MEM" \
    -serial stdio -display none -no-reboot \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 >"$LOG" 2>&1
QEXIT=$?

if [ "$QUIET" = 0 ]; then
    echo "----- console série -----"; cat "$LOG"; echo "-------------------------"
fi

fail=0
check()  { if grep -qE "$1" "$LOG"; then echo "PASS: $2"; else echo "FAIL: $2 (motif /$1/)"; fail=1; fi; }
refute() { if grep -qE "$1" "$LOG"; then echo "FAIL: $2 (motif interdit /$1/)"; fail=1; else echo "PASS: $2"; fi; }

# HANG detection : timeout(1) renvoie 124 s'il a dû tuer QEMU.
if [ "$QEXIT" = 124 ]; then echo "FAIL: QEMU n'a pas terminé (HANG / timeout)"; fail=1; fi

# Phase 1
check "MADT found"                                "Phase 1: MADT"
check "ASSERT regions disjoint & sorted: PASS"    "Phase 1: invariant RAM"
check "Phase 1 complete"                          "Phase 1: fin"
# Phase 2
check "APs: expected=$EXPECTED started=$EXPECTED"  "tous les AP démarrés"
check "ASSERT started == expected: PASS"          "assertion démarrage"
check "isolation: pass=$EXPECTED fail=0"          "isolation: pass==expected, fail==0"
check "ASSERT isolation enforced: PASS"           "assertion isolation"
check "Phase 2 complete"                          "Phase 2: fin"
if [ "$EXPECTED" -gt 0 ]; then
    check "ISOLATION OK"                          "au moins un #PF croisé capturé"
fi
# Interdits
refute "ISOLATION FAIL"                           "aucune violation d'isolation"
refute "UNEXPECTED EXCEPTION"                     "aucune exception inattendue"
refute "FAILED to start"                          "aucun AP en échec de démarrage"
refute "FATAL"                                    "aucun message FATAL"

[ "$LOG" = "${LOG_KEEP:-}" ] || rm -f "$LOG"
if [ "$fail" -eq 0 ]; then echo "== RESULT: PHASE 2 OK (smp=$SMP mem=$MEM) =="; exit 0
else echo "== RESULT: PHASE 2 ÉCHEC (smp=$SMP mem=$MEM) =="; exit 1; fi
