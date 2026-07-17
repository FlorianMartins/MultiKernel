#!/usr/bin/env bash
# NEXUS-OS — Phase 6, test W^X / NX.
# Build NODEL_WX_TEST=1 : le userland Node-L tente d'exécuter depuis sa pile (NX)
# -> #PF. Prouve que la pile user n'est pas exécutable (W^X appliqué).
set -u
cd "$(dirname "$0")/../.." || exit 2

SMP="${SMP:-4}"; MEM="${MEM:-512}"; TIMEOUT="${TIMEOUT:-60}"
LOG="$(mktemp)"

echo "== Build NODEL_WX_TEST=1 =="
rm -f build/nodel_user.elf build/NODE-L/userland/user_blob.o build/nexus.elf build/nexus-os.iso
make NODEL_WX_TEST=1 iso >/dev/null 2>&1 || { echo "FAIL: build"; exit 2; }

echo "== Boot (le userland tente d'exécuter sa pile NX) =="
printf 'exit\n' | timeout "$TIMEOUT" qemu-system-x86_64 \
    -cdrom build/nexus-os.iso -smp "$SMP" -m "$MEM" \
    -serial stdio -display none -no-reboot \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 >"$LOG" 2>&1

echo "----- extrait -----"
grep -nE "W\^X test|executing from|#PF @0x4[0-9a-f]|ISOLATION OK|W\^X test FAILED" "$LOG" | head -15
echo "-------------------"

fail=0
check()  { if grep -qE "$1" "$LOG"; then echo "PASS: $2"; else echo "FAIL: $2 (motif /$1/)"; fail=1; fi; }
refute() { if grep -qE "$1" "$LOG"; then echo "FAIL: $2 (interdit /$1/)"; fail=1; else echo "PASS: $2"; fi; }

check "executing from NX stack"        "userland: tentative d'exécution depuis la pile"
check "#PF @0x4[0-9a-f]{6}"            "exécution pile -> #PF (NX appliqué)"
refute "W\^X test FAILED"              "la pile n'était PAS exécutable (W^X OK)"

# rebuild nominal
rm -f build/nodel_user.elf build/NODE-L/userland/user_blob.o build/nexus.elf build/nexus-os.iso
make iso >/dev/null 2>&1

rm -f "$LOG"
if [ "$fail" -eq 0 ]; then echo "== RESULT: W^X / NX OK =="; exit 0
else echo "== RESULT: W^X ÉCHEC =="; exit 1; fi
