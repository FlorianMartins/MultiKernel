#!/usr/bin/env bash
# NEXUS-OS — Phase 9, test framebuffer (Multiboot2 + rendu 2D).
# Vérifie que GRUB fournit un framebuffer linéaire, que le splash est dessiné, et
# (screendump QEMU + PIL) que l'écran contient réellement du contenu graphique.
set -u
cd "$(dirname "$0")/../.." || exit 2

SMP="${SMP:-4}"; MEM="${MEM:-512}"
ISO="${ISO:-build/nexus-os.iso}"
[ -f "$ISO" ] || { echo "FAIL: ISO introuvable ($ISO)"; exit 2; }

T="$(mktemp -d)"
SER="$T/serial.txt"; SOCK="$T/mon.sock"; PPM="$T/screen.ppm"

qemu-system-x86_64 -cdrom "$ISO" -smp "$SMP" -m "$MEM" \
    -serial file:"$SER" -display none -no-reboot -vga std \
    -monitor unix:"$SOCK",server,nowait &
QPID=$!

for i in $(seq 1 60); do grep -q "splash" "$SER" 2>/dev/null && break; sleep 0.1; done
sleep 0.5
SOCK="$SOCK" PPM="$PPM" python3 - <<'PY'
import socket,os,time
s=socket.socket(socket.AF_UNIX); s.connect(os.environ["SOCK"]); time.sleep(0.3); s.recv(4096)
s.sendall(("screendump %s\n"%os.environ["PPM"]).encode()); time.sleep(0.8)
PY
sleep 0.3; kill $QPID 2>/dev/null; wait 2>/dev/null

fail=0
check() { if grep -qE "$1" "$SER"; then echo "PASS: $2"; else echo "FAIL: $2 (motif /$1/)"; fail=1; fi; }
check "\[fb\] 1024x768 32bpp"       "framebuffer 1024x768 32bpp obtenu de GRUB"
check "\[splash\]"                  "splash dessiné"

# Analyse d'image : présence de pixels cyan accent (logo/titre) + variété de couleurs.
if [ -f "$PPM" ]; then
  PPM="$PPM" python3 - <<'PY'
import os
from PIL import Image
im=Image.open(os.environ["PPM"]).convert("RGB")
W,Hh=im.size
px=im.load()
cyan=0; colors=set()
for y in range(0,Hh,4):
    for x in range(0,W,4):
        r,g,b=px[x,y]
        colors.add((r//32,g//32,b//32))
        if b>150 and g>140 and r<120:   # cyan/bleu clair (accent MultiKernel)
            cyan+=1
print("  cyan-ish samples:",cyan," | distinct color buckets:",len(colors))
ok = cyan>200 and len(colors)>8
print("  PASS: contenu graphique non trivial" if ok else "  FAIL: écran quasi vide")
import sys; sys.exit(0 if ok else 1)
PY
  [ $? -eq 0 ] || fail=1
else
  echo "FAIL: pas de screendump"; fail=1
fi

rm -rf "$T"
if [ "$fail" -eq 0 ]; then echo "== RESULT: FRAMEBUFFER OK =="; exit 0
else echo "== RESULT: FRAMEBUFFER ÉCHEC =="; exit 1; fi
