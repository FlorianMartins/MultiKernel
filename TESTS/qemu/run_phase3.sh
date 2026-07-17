#!/usr/bin/env bash
# NEXUS-OS — validation Phase 3 (superset : Phases 1+2+3) sous QEMU.
# Le noyau imprime ses propres verdicts "ASSERT ... : PASS" ; on les grep
# et on refuse tout FAIL/FATAL/HANG. QEMU sort seul via isa-debug-exit.
set -u

SMP="${SMP:-4}"
MEM="${MEM:-512}"
ISO="${ISO:-build/nexus-os.iso}"
LOG="$(mktemp)"
TIMEOUT="${TIMEOUT:-30}"
QUIET="${QUIET:-0}"

[ -f "$ISO" ] || { echo "FAIL: ISO introuvable ($ISO)"; exit 2; }

[ "$QUIET" = 0 ] && echo "== Boot QEMU (smp=$SMP, mem=${MEM}M, timeout=${TIMEOUT}s) =="
timeout "$TIMEOUT" qemu-system-x86_64 \
    -cdrom "$ISO" -smp "$SMP" -m "$MEM" \
    -serial stdio -display none -no-reboot \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 >"$LOG" 2>&1
QEXIT=$?

[ "$QUIET" = 0 ] && { echo "----- console série -----"; cat "$LOG"; echo "-------------------------"; }

fail=0
check()  { if grep -qE "$1" "$LOG"; then echo "PASS: $2"; else echo "FAIL: $2 (motif /$1/)"; fail=1; fi; }
refute() { if grep -qE "$1" "$LOG"; then echo "FAIL: $2 (interdit /$1/)"; fail=1; else echo "PASS: $2"; fi; }

[ "$QEXIT" = 124 ] && { echo "FAIL: QEMU n'a pas terminé (HANG/timeout)"; fail=1; }

# Phase 1
check "MADT found"                                "Phase 1: MADT"
check "ASSERT regions disjoint & sorted: PASS"    "Phase 1: invariant RAM"
# Phase 2
check "ASSERT started == expected: PASS"          "Phase 2: tous les AP démarrés"
check "ASSERT isolation enforced: (PASS|SKIP)"    "Phase 2: isolation (PASS/SKIP)"
# Phase 3
check "ASSERT delivery FIFO\+lossless: (PASS|SKIP)" "Phase 3: livraison IPC (PASS/SKIP)"
# Si l'IPC est actif (>=1 Node-L + 1 Node-W), on exige les PASS forts
if [ "$SMP" -ge 3 ]; then
    check "ASSERT delivery FIFO\+lossless: PASS"   "Phase 3: IPC FIFO+lossless"
    check "ASSERT doorbell wake: PASS"             "Phase 3: doorbell réveille le hlt"
    check "CONSUMER got=100000 order=OK checksum=OK" "Phase 3: consommateur 100k OK"
fi
check "Phase 3 complete"                           "fin de Phase 3"
# Interdits
refute "ISOLATION FAIL"                            "aucune violation d'isolation"
refute "UNEXPECTED EXCEPTION"                      "aucune exception inattendue"
refute "FAILED to start"                           "aucun AP en échec"
refute ": FAIL"                                    "aucun ASSERT en échec"
refute "FATAL"                                     "aucun FATAL"

rm -f "$LOG"
if [ "$fail" -eq 0 ]; then echo "== RESULT: PHASE 3 OK (smp=$SMP mem=$MEM) =="; exit 0
else echo "== RESULT: PHASE 3 ÉCHEC (smp=$SMP mem=$MEM) =="; exit 1; fi
