#!/usr/bin/env bash
# NEXUS-OS — Phase 6, test FAULT-CONTAINMENT + HOT RESTART.
# Build NODEW_FAULT_ONCE=1 : Node-W faute au 1er boot, le Coordinator détecte,
# contient, redémarre à chaud -> Node-W recouvré ; Node-L jamais perturbé.
set -u
cd "$(dirname "$0")/../.." || exit 2

SMP="${SMP:-4}"; MEM="${MEM:-512}"; TIMEOUT="${TIMEOUT:-90}"
LOG="$(mktemp)"

echo "== Build NODEW_FAULT_ONCE=1 =="
rm -f build/NODE-W/kernel/nodew.o build/nexus.elf build/nexus-os.iso
make NODEW_FAULT_ONCE=1 iso >/dev/null 2>&1 || { echo "FAIL: build"; exit 2; }

echo "== Boot (Node-W va fauter puis être redémarré) =="
printf 'help\nexit\n' | timeout "$TIMEOUT" qemu-system-x86_64 \
    -cdrom build/nexus-os.iso -smp "$SMP" -m "$MEM" \
    -serial stdio -display none -no-reboot \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 >"$LOG" 2>&1

echo "----- extrait -----"
grep -nE "attempt #|simulating fault|FAULT detected|hot-restart|RECOVERED|recovered on restart|node-l alive|both nodes" "$LOG" | head -25
echo "-------------------"

fail=0
check() { if grep -qE "$1" "$LOG"; then echo "PASS: $2"; else echo "FAIL: $2 (motif /$1/)"; fail=1; fi; }

check "boot \(attempt #1\)"                          "Node-W: 1er boot"
check "simulating fault"                             "Node-W: faute déclenchée au 1er boot"
check "FAULT detected"                               "Coordinator: faute détectée"
check "hot-restarting Node-W"                        "Coordinator: redémarrage à chaud lancé"
check "boot \(attempt #2\)"                          "Node-W: relancé (2e boot)"
check "recovered on restart"                         "Node-W: reprise nominale"
check "Node-W RECOVERED after hot restart"           "Coordinator: reprise confirmée"
check "ASSERT Node-W recovered: PASS"                "assert reprise Node-W"
check "ASSERT Node-L survived Node-W restart: PASS"  "Node-L: indemne pendant le restart"
check "ASSERT node-l alive \(heartbeat\): PASS"      "Node-L: vivant"

# rebuild nominal
rm -f build/NODE-W/kernel/nodew.o build/nexus.elf build/nexus-os.iso
make iso >/dev/null 2>&1

rm -f "$LOG"
if [ "$fail" -eq 0 ]; then echo "== RESULT: FAULT-CONTAINMENT + HOT RESTART OK =="; exit 0
else echo "== RESULT: ÉCHEC =="; exit 1; fi
