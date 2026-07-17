#!/usr/bin/env bash
# NEXUS-OS — Phase 8, test du clavier PS/2 bufferisé par interruption.
# Injecte des touches via le monitor QEMU (sendkey) -> 8042 -> IRQ1 -> IO-APIC -> LAPIC
# -> ISR -> ring buffer -> shell Node-L. Vérifie que le shell reçoit/exécute les touches.
set -u
cd "$(dirname "$0")/../.." || exit 2

SMP="${SMP:-4}"; MEM="${MEM:-512}"
ISO="${ISO:-build/nexus-os.iso}"
[ -f "$ISO" ] || { echo "FAIL: ISO introuvable ($ISO)"; exit 2; }

T="$(mktemp -d)"
SER="$T/serial.txt"; SOCK="$T/mon.sock"

: > "$SER"
qemu-system-x86_64 -cdrom "$ISO" -smp "$SMP" -m "$MEM" \
    -serial file:"$SER" -display none -no-reboot \
    -monitor unix:"$SOCK",server,nowait &
QPID=$!

# Attendre que le shell Node-L soit PRÊT (prompt affiché) avant d'injecter -> déterministe.
for i in $(seq 1 100); do
    grep -q "mini-shell" "$SER" 2>/dev/null && break
    sleep 0.1
done
sleep 0.4

SER="$SER" python3 - "$SOCK" <<'PY'
import socket,sys,time,os
s=socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); time.sleep(0.3); s.recv(4096)
ser=os.environ["SER"]
def key(k):
    s.sendall(("sendkey "+k+"\n").encode()); time.sleep(0.15)
    try: s.recv(4096)
    except: pass
def line(keys, wait_for):
    for k in keys: key(k)
    key("ret")
    for _ in range(30):                # attendre l'écho/exécution avant la ligne suivante
        if wait_for in open(ser, errors="replace").read(): return
        time.sleep(0.1)
line(["h","e","l","p"], "cette aide")
line(["p","s"], "PID  CMD")
line(["e","x","i","t"], "bye")
time.sleep(0.5)
PY

sleep 1
kill $QPID 2>/dev/null; wait 2>/dev/null

fail=0
check() { if grep -qE "$1" "$SER"; then echo "PASS: $2"; else echo "FAIL: $2 (motif /$1/)"; fail=1; fi; }
refute(){ if grep -qE "$1" "$SER"; then echo "FAIL: $2 (interdit /$1/)"; fail=1; else echo "PASS: $2"; fi; }

check "IRQ1 -> GSI"                       "IO-APIC : IRQ1 routée"
check "GSI[0-9]+ -> vec 0x21"             "IO-APIC : GSI clavier -> vecteur 0x21"
check "clavier PS/2 initialis"            "8042 initialisé"
check "IF=1, clavier actif"              "ring 3 avec IF=1"
check "prism:/ \\\$ help"                "clavier : 'help' saisi via IRQ"
check "help  - cette aide"               "clavier : 'help' exécuté"
check "PID  CMD"                          "clavier : 'ps' exécuté"
check "\\[init\\] bye"                    "clavier : 'exit' exécuté"
refute "#PF @0xfee"                       "aucun #PF sur la MMIO LAPIC"
refute "ISOLATION FAIL"                   "aucune violation d'isolation"

rm -rf "$T"
if [ "$fail" -eq 0 ]; then echo "== RESULT: CLAVIER PS/2 (IRQ) OK =="; exit 0
else echo "== RESULT: CLAVIER PS/2 ÉCHEC =="; exit 1; fi
