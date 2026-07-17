#!/usr/bin/env bash
# NEXUS-OS — Phase 10, test souris PS/2 (IRQ12) + double buffering.
# Build GFX_DEMO=1 : boucle graphique double-bufferisée avec curseur souris.
# Injecte du mouvement via le monitor QEMU (mouse_move) -> IRQ12 -> ISR -> curseur bouge.
set -u
cd "$(dirname "$0")/../.." || exit 2

SMP="${SMP:-4}"; MEM="${MEM:-512}"
T="$(mktemp -d)"; SER="$T/serial.txt"; SOCK="$T/mon.sock"; PPM="$T/screen.ppm"

echo "== Build GFX_DEMO=1 =="
# main.o dépend du flag GFX_DEMO (make ne suit pas les CFLAGS) -> le forcer à recompiler.
rm -f build/COORDINATOR/core/main.o build/nexus.elf build/nexus-os.iso
make GFX_DEMO=1 iso >/dev/null 2>&1 || { echo "FAIL: build"; exit 2; }

qemu-system-x86_64 -cdrom build/nexus-os.iso -smp "$SMP" -m "$MEM" \
    -serial file:"$SER" -display none -no-reboot -vga std \
    -monitor unix:"$SOCK",server,nowait &
QPID=$!

for i in $(seq 1 100); do grep -q "démo souris" "$SER" 2>/dev/null && break; sleep 0.1; done
sleep 0.4
SOCK="$SOCK" PPM="$PPM" python3 - <<'PY'
import socket,os,time
s=socket.socket(socket.AF_UNIX); s.connect(os.environ["SOCK"]); time.sleep(0.3); s.recv(4096)
def cmd(c):
    s.sendall((c+"\n").encode()); time.sleep(0.18)
    try: s.recv(4096)
    except: pass
for _ in range(5): cmd("mouse_move 25 -18")   # mouvement modéré (droite + haut)
time.sleep(0.5)
cmd("screendump %s"%os.environ["PPM"]); time.sleep(0.8)
PY
sleep 1; kill $QPID 2>/dev/null; wait 2>/dev/null

fail=0
check() { if grep -qE "$1" "$SER"; then echo "PASS: $2"; else echo "FAIL: $2 (motif /$1/)"; fail=1; fi; }

check "souris PS/2 initialis"                 "souris PS/2 initialisée (IRQ12)"
check "GSI[0-9]+ -> vec 0x22"                 "IO-APIC : IRQ12 -> vecteur 0x22"
check "mouse packets=[1-9]"                   "paquets souris reçus (IRQ12 délivrée)"
check "double-buffering utilisé: PASS"        "double buffering (fb_present par frame)"

# La position finale doit avoir changé par rapport au centre (512,384).
if grep -qE "curseur final \(512,384\)" "$SER"; then
    echo "FAIL: le curseur n'a pas bougé"; fail=1
else
    grep -qE "curseur final \([0-9-]+,[0-9-]+\)" "$SER" && echo "PASS: curseur déplacé par la souris" || { echo "FAIL: pas de position finale"; fail=1; }
fi

# Screendump : contenu graphique présent
if [ -f "$PPM" ]; then
  PPM="$PPM" python3 - <<'PY'
from PIL import Image; import os,sys
im=Image.open(os.environ["PPM"]).convert("RGB"); W,H=im.size; px=im.load()
cyan=sum(1 for y in range(0,H,4) for x in range(0,W,4) if px[x,y][2]>150 and px[x,y][1]>140 and px[x,y][0]<120)
print("  PASS: scène GUI rendue" if cyan>100 else "  FAIL: écran vide")
sys.exit(0 if cyan>100 else 1)
PY
  [ $? -eq 0 ] || fail=1
fi

# rebuild nominal (recompiler main.o SANS le flag)
rm -f build/COORDINATOR/core/main.o build/nexus.elf build/nexus-os.iso; make iso >/dev/null 2>&1
rm -rf "$T"
if [ "$fail" -eq 0 ]; then echo "== RESULT: SOURIS + DOUBLE BUFFERING OK =="; exit 0
else echo "== RESULT: ÉCHEC =="; exit 1; fi
