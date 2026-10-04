#!/usr/bin/env bash
# NEXUS-OS — validation Phase 5 (superset : Phases 1..5) sous QEMU.
# Node-W (PE/NT) tourne en parallèle de Node-L, avec I/O croisée via IPC.
set -u

SMP="${SMP:-4}"
MEM="${MEM:-512}"
ISO="${ISO:-build/nexus-os.iso}"
LOG="$(mktemp)"
TIMEOUT="${TIMEOUT:-60}"
QUIET="${QUIET:-0}"

[ -f "$ISO" ] || { echo "FAIL: ISO introuvable ($ISO)"; exit 2; }

[ "$QUIET" = 0 ] && echo "== Boot QEMU (smp=$SMP, mem=${MEM}M, timeout=${TIMEOUT}s) =="
printf 'help\nexit\n' | \
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

# Phases 1-4
check "ASSERT regions disjoint & sorted: PASS"       "Phase 1: invariant RAM"
check "ASSERT started == expected: PASS"             "Phase 2: AP démarrés"
check "ASSERT isolation enforced: (PASS|SKIP)"       "Phase 2: isolation"
check "ASSERT delivery FIFO\+lossless: (PASS|SKIP)"  "Phase 3: IPC"
# Node-L (>=2 cœurs)
if [ "$SMP" -ge 2 ]; then
    check "ASSERT node-l alive \(heartbeat\): PASS"  "Phase 4: Node-L vivant"
fi
# Phase 5 — Node-W (nécessite un cœur Node-W : SMP >= 3)
if [ "$SMP" -ge 3 ]; then
    check "Node-W \(NT-compat\) kernel boot"         "Node-W: boot"
    check "own paging active"                        "Node-W: pagination propre (partagée avec L)"
    check "PE OK: base=0x8400000"                    "Node-W: PE chargé"
    check "entering ring 3 @0x8401000"               "Node-W: passage ring 3 (PE)"
    check "MultiKernel Node-W  --  PE32\+ native"          "Node-W: NtDisplayString (bannière PE)"
    check "cross-node I/O verified"                  "Node-W: I/O croisée vérifiée"
    check "ASSERT PE loaded: PASS"                   "Node-W: assert PE"
    check "ASSERT ring3 PE ran\+terminated: PASS"    "Node-W: PE exécuté+terminé"
    check "ASSERT cross-node I/O round-trip: PASS"   "Node-W: assert I/O round-trip"
    check "ASSERT node-w alive \(heartbeat\): PASS"  "Node-W: heartbeat vu par le BSP"
    check "ASSERT PE terminated cleanly: PASS"       "Node-W: NtTerminateProcess"
    check "ASSERT cross-node I/O verified: PASS"     "Node-W: I/O vérifiée (BSP)"
    check "ASSERT both nodes alive in parallel: PASS" "les DEUX OS tournent en parallèle"
else
    check "node-w alive: SKIP"                       "Node-W SKIP (aucun cœur Node-W)"
fi
check "Phase [5-9] complete"                             "fin de Phase 5"
# Interdits
refute "ISOLATION FAIL"                              "aucune violation d'isolation"
refute "UNEXPECTED EXCEPTION"                        "aucune exception inattendue"
refute "PE load FAILED"                              "chargement PE OK"
refute "I/O MISMATCH"                                "aucun mismatch I/O"
refute ": FAIL"                                      "aucun ASSERT en échec"
refute "FATAL"                                       "aucun FATAL"

rm -f "$LOG"
if [ "$fail" -eq 0 ]; then echo "== RESULT: PHASE 5 OK (smp=$SMP mem=$MEM) =="; exit 0
else echo "== RESULT: PHASE 5 ÉCHEC (smp=$SMP mem=$MEM) =="; exit 1; fi
