#!/usr/bin/env bash
# NEXUS-OS — Phase 5, test de CONFINEMENT (security-model C5).
# Construit un .exe Node-W qui déréférence la RAM de Node-L (accès cross-domaine),
# et vérifie que : (a) l'accès faute (#PF -> confiné), (b) Node-L SURVIT (heartbeat +
# userland OK). Prouve qu'un nœud vérolé ne compromet pas l'autre.
set -u
cd "$(dirname "$0")/../.." || exit 2

SMP="${SMP:-4}"
MEM="${MEM:-512}"
TIMEOUT="${TIMEOUT:-90}"
LOG="$(mktemp)"

echo "== Build variante NODEW_CRASH=1 =="
rm -f build/hello_pe.exe build/NODE-W/subsystems/pe_blob.o build/nexus.elf build/nexus-os.iso
make NODEW_CRASH=1 iso >/dev/null 2>&1 || { echo "FAIL: build crash"; exit 2; }

echo "== Boot (le .exe Node-W va toucher la RAM Node-L) =="
printf 'help\nexit\n' | \
timeout "$TIMEOUT" qemu-system-x86_64 \
    -cdrom build/nexus-os.iso -smp "$SMP" -m "$MEM" \
    -serial stdio -display none -no-reboot \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 >"$LOG" 2>&1

echo "----- console série (extrait) -----"
grep -nE "crash mode|#PF|node-l alive|ring3 userland|Node-W|both nodes" "$LOG" | head -30
echo "-----------------------------------"

fail=0
check()  { if grep -qE "$1" "$LOG"; then echo "PASS: $2"; else echo "FAIL: $2 (motif /$1/)"; fail=1; fi; }

# Le .exe a atteint le point de crash puis a fauté sur la RAM Node-L
check "crash mode\) touching Node-L RAM"        "le .exe Node-W tente l'accès cross-domaine"
check "#PF @0x4000000"                           "l'accès à la RAM Node-L a FAUTÉ (confiné)"
# Node-L a survécu au crash de Node-W
check "ASSERT ring3 userland: PASS"              "Node-L: userland a tourné normalement"
check "ASSERT node-l alive \(heartbeat\): PASS"  "Node-L: TOUJOURS VIVANT malgré le crash de Node-W"

# Rebuild propre (variante nominale) pour ne pas laisser l'ISO en mode crash
rm -f build/hello_pe.exe build/NODE-W/subsystems/pe_blob.o build/nexus.elf build/nexus-os.iso
make iso >/dev/null 2>&1

rm -f "$LOG"
if [ "$fail" -eq 0 ]; then
    echo "== RESULT: CONFINEMENT OK — un nœud vérolé ne compromet pas l'autre =="
    exit 0
else
    echo "== RESULT: CONFINEMENT ÉCHEC =="
    exit 1
fi
