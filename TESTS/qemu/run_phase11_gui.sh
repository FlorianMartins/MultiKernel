#!/usr/bin/env bash
# NEXUS-OS — Phase 11, test compositeur userland (Node-L, ring 3).
# Build NODEL_GUI=1 : desktop + fenêtres + curseur souris, double-bufferisé.
# Injecte souris (mouse_move / mouse_button) via le monitor QEMU et vérifie que le
# compositeur rend, suit la souris, et détecte les clics / drags. Screendump à l'appui.
set -u
cd "$(dirname "$0")/../.." || exit 2

SMP="${SMP:-4}"; MEM="${MEM:-512}"
T="$(mktemp -d)"; SER="$T/serial.txt"; SOCK="$T/mon.sock"; PPM="$T/screen.ppm"

echo "== Build NODEL_GUI=1 =="
rm -f build/nodel_user.elf build/NODE-L/userland/user_blob.o build/COORDINATOR/core/main.o build/nexus.elf build/nexus-os.iso
make NODEL_GUI=1 iso >/dev/null 2>&1 || { echo "FAIL: build"; exit 2; }

qemu-system-x86_64 -cdrom build/nexus-os.iso -smp "$SMP" -m "$MEM" \
    -serial file:"$SER" -display none -no-reboot -vga std \
    -monitor unix:"$SOCK",server,nowait &
QPID=$!
for i in $(seq 1 120); do grep -q "compositeur demarre" "$SER" 2>/dev/null && break; sleep 0.1; done
sleep 0.6

SOCK="$SOCK" PPM="$PPM" python3 - <<'PY'
import socket,os,time
s=socket.socket(socket.AF_UNIX); s.connect(os.environ["SOCK"]); time.sleep(0.25); s.recv(4096)
def cmd(c):
    s.sendall((c+"\n").encode()); time.sleep(0.16)
    try: s.recv(4096)
    except: pass
# amener le curseur dans la fenêtre "Systeme" (bas-gauche) via clamp fiable, puis drag.
for _ in range(4): cmd("mouse_move -200 -200")   # coin bas-gauche (0,~767)
cmd("mouse_move 200 0")                            # x -> ~200 (dans Systeme)
cmd("mouse_move 0 -200")                           # remonter dans Systeme
cmd("mouse_button 1")                              # grab
for _ in range(4): cmd("mouse_move 60 -30")        # drag
cmd("mouse_button 0")                              # drop
time.sleep(0.4)
cmd("screendump %s"%os.environ["PPM"]); time.sleep(0.8)
PY
sleep 0.5; kill $QPID 2>/dev/null; wait 2>/dev/null

echo "----- événements gui -----"; grep -E "\[gui\]" "$SER" | tail -14; echo "--------------------------"

fail=0
check() { if grep -qE "$1" "$SER"; then echo "PASS: $2"; else echo "FAIL: $2 (motif /$1/)"; fail=1; fi; }
check "compositeur demarre \(1024x768\)"   "compositeur démarré en ring 3"
check "\[gui\] cursor=\("                   "curseur suivi (souris lue par le compositeur)"
check "\[gui\] click @\("                   "clic détecté + hit-test"
# drag & drop d'une fenêtre + z-order
check "window '.*' -> drag start"          "fenêtre saisie (drag start + z-order)"
check "\[gui\] drop "                       "fenêtre déplacée (drop)"

# rendu : le screendump doit contenir les fenêtres (couleurs accent)
if [ -f "$PPM" ]; then
  PPM="$PPM" python3 - <<'PY'
from PIL import Image; import os,sys
im=Image.open(os.environ["PPM"]).convert("RGB"); W,H=im.size; px=im.load()
# titres de fenêtres cyan/vert/bleu clair -> beaucoup de pixels clairs bleu-vert
accent=sum(1 for y in range(0,H,3) for x in range(0,W,3) if px[x,y][1]>140 and px[x,y][2]>150)
print("  PASS: desktop + fenêtres rendus" if accent>300 else "  FAIL: rendu insuffisant")
sys.exit(0 if accent>300 else 1)
PY
  [ $? -eq 0 ] || fail=1
else echo "FAIL: pas de screendump"; fail=1; fi

# rebuild nominal (shell)
rm -f build/nodel_user.elf build/NODE-L/userland/user_blob.o build/COORDINATOR/core/main.o build/nexus.elf build/nexus-os.iso
make iso >/dev/null 2>&1
rm -rf "$T"
if [ "$fail" -eq 0 ]; then echo "== RESULT: COMPOSITEUR OK =="; exit 0
else echo "== RESULT: ÉCHEC =="; exit 1; fi
