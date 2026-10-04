#!/usr/bin/env bash
# NEXUS-OS — validation Phase 4 (superset : Phases 1+2+3+4) sous QEMU.
# Node-L souverain : pagination propre, ordonnanceur, userland ring 3, syscalls, heartbeat.
# On injecte des commandes shell via stdin (piped -> COM1). QEMU sort seul (isa-debug-exit).
set -u

SMP="${SMP:-4}"
MEM="${MEM:-512}"
ISO="${ISO:-build/nexus-os.iso}"
LOG="$(mktemp)"
TIMEOUT="${TIMEOUT:-60}"
QUIET="${QUIET:-0}"

[ -f "$ISO" ] || { echo "FAIL: ISO introuvable ($ISO)"; exit 2; }

[ "$QUIET" = 0 ] && echo "== Boot QEMU (smp=$SMP, mem=${MEM}M, timeout=${TIMEOUT}s) =="
# Entrée shell injectée : help, echo, ps, exit.
printf 'help\necho hello-nexus\nps\nexit\n' | \
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

# Phases 1-3
check "ASSERT regions disjoint & sorted: PASS"       "Phase 1: invariant RAM"
check "ASSERT started == expected: PASS"             "Phase 2: AP démarrés"
check "ASSERT isolation enforced: (PASS|SKIP)"       "Phase 2: isolation"
check "ASSERT delivery FIFO\+lossless: (PASS|SKIP)"  "Phase 3: IPC"
# Phase 4 — Node-L souverain (nécessite >= 1 cœur AP Node-L, donc SMP >= 2)
if [ "$SMP" -ge 2 ]; then
    check "own paging active"                        "Node-L: pagination propre"
    check "ASSERT own-paging: PASS"                  "Node-L: bascule CR3"
    check "ASSERT scheduler \(2 tasks\): PASS"       "Node-L: ordonnanceur 2 tâches"
    check "task A tick 0"                            "Node-L: task A ordonnancée"
    check "task B tick 0"                            "Node-L: task B ordonnancée"
    check "entering ring 3"                          "Node-L: passage ring 3"
    check "MultiKernel Node-L userland"                 "Node-L: userland exécuté (ring 3)"
    check "ASSERT ring3 userland: PASS"              "Node-L: userland a fait SYS_exit"
    check "ASSERT user exit code 0: PASS"            "Node-L: exit code 0"
    check "ASSERT node-l alive \(heartbeat\): PASS"  "Node-L: heartbeat vu par le BSP"
    # commandes shell exécutées (entrée injectée)
    check "hello-nexus"                              "shell: echo exécuté"
    check "PID  CMD"                                 "shell: ps exécuté"
    check "\[init\] bye"                             "shell: exit exécuté"
else
    check "node-l alive: SKIP"                       "Node-L SKIP (aucun cœur Node-L à SMP=1)"
fi
check "Phase 4 (complete|FAILED)"                    "verdict Phase 4 émis"
check "Phase 4 complete"                             "fin de Phase 4"
# Interdits
refute "ISOLATION FAIL"                              "aucune violation d'isolation"
refute "UNEXPECTED EXCEPTION"                        "aucune exception inattendue"
refute "ELF load FAILED"                             "chargement ELF OK"
refute ": FAIL"                                      "aucun ASSERT en échec"
refute "FATAL"                                       "aucun FATAL"

rm -f "$LOG"
if [ "$fail" -eq 0 ]; then echo "== RESULT: PHASE 4 OK (smp=$SMP mem=$MEM) =="; exit 0
else echo "== RESULT: PHASE 4 ÉCHEC (smp=$SMP mem=$MEM) =="; exit 1; fi
