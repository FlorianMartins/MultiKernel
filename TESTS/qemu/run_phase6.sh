#!/usr/bin/env bash
# NEXUS-OS — validation Phase 6 nominale (superset : Phases 1..6) sous QEMU.
# Durcissement : IOMMU détectée, checklist anti-cheat, monitor (nœuds sains).
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

# Phases 1-5 (résumé)
check "ASSERT regions disjoint & sorted: PASS"       "Phase 1: invariant RAM"
check "ASSERT started == expected: PASS"             "Phase 2: AP démarrés"
check "ASSERT isolation enforced: (PASS|SKIP)"       "Phase 2: isolation"
check "ASSERT delivery FIFO\+lossless: (PASS|SKIP)"  "Phase 3: IPC"
if [ "$SMP" -ge 2 ]; then check "ASSERT node-l alive \(heartbeat\): PASS" "Phase 4: Node-L vivant"; fi
if [ "$SMP" -ge 3 ]; then check "ASSERT both nodes alive in parallel: PASS" "Phase 5: 2 OS en parallèle"; fi
# Phase 6 — durcissement
check "\[iommu\] ASSERT (IOMMU detected|DMA isolation)"  "Phase 6: détection IOMMU (DMAR)"
check "\[anticheat\] --- readiness checklist ---"        "Phase 6: checklist anti-cheat"
check "hypervisor bit \(CPUID"                           "Phase 6: bit hyperviseur journalisé"
check "invariant TSC"                                    "Phase 6: TSC journalisé"
if [ "$SMP" -ge 3 ]; then
    check "ASSERT nodes healthy \(no restart needed\): PASS" "Phase 6: monitor — nœuds sains"
fi
check "Phase 6 complete"                                 "fin de Phase 6"
# Interdits
refute "ISOLATION FAIL"                                  "aucune violation d'isolation"
refute "UNEXPECTED EXCEPTION"                            "aucune exception inattendue"
refute "FAULT detected"                                  "aucune faute nœud (nominal)"
refute ": FAIL"                                          "aucun ASSERT en échec"
refute "FATAL"                                           "aucun FATAL"

rm -f "$LOG"
if [ "$fail" -eq 0 ]; then echo "== RESULT: PHASE 6 OK (smp=$SMP mem=$MEM) =="; exit 0
else echo "== RESULT: PHASE 6 ÉCHEC (smp=$SMP mem=$MEM) =="; exit 1; fi
