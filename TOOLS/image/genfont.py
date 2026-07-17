#!/usr/bin/env python3
"""Génère HAL/gpu/font8x16.h (fonte bitmap 8x16, ASCII 0x20..0x7E) depuis une TTF
monospace. À relancer seulement si on veut changer de fonte. Sortie committée -> pas
de dépendance au build.  Usage: python3 TOOLS/image/genfont.py"""
from PIL import Image, ImageFont, ImageDraw

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
W, H = 8, 16

def pick_size():
    for size in range(18, 12, -1):
        f = ImageFont.truetype(FONT, size)
        d = ImageDraw.Draw(Image.new("L", (40, 40)))
        b = d.textbbox((0, 0), "M", font=f)
        if b[2] - b[0] <= W and b[3] - b[1] <= H:
            return size, f
    return 13, ImageFont.truetype(FONT, 13)

def main():
    size, font = pick_size()
    rows_all = []
    for code in range(0x20, 0x7F):
        im = Image.new("L", (W, H), 0)
        d = ImageDraw.Draw(im)
        bbox = d.textbbox((0, 0), chr(code), font=font)
        ox = (W - (bbox[2] - bbox[0])) // 2 - bbox[0]
        d.text((ox, 1), chr(code), fill=255, font=font)
        rows = []
        for y in range(H):
            byte = 0
            for x in range(W):
                if im.getpixel((x, y)) > 110:
                    byte |= (1 << (7 - x))
            rows.append(byte)
        rows_all.append((code, rows))

    with open("HAL/gpu/font8x16.h", "w") as o:
        o.write("/* NEXUS-OS — fonte bitmap 8x16, ASCII imprimable 0x20..0x7E.\n")
        o.write(" * Générée depuis DejaVu Sans Mono (TOOLS/image/genfont.py) — pas de dép. build.\n")
        o.write(" * font8x16[c-0x20][row] : bit 7 = pixel gauche. */\n")
        o.write('#pragma once\n#include "kc/types.h"\n\n')
        o.write("#define FONT_FIRST 0x20\n#define FONT_LAST  0x7E\n#define FONT_W 8\n#define FONT_H 16\n\n")
        o.write("static const u8 font8x16[FONT_LAST-FONT_FIRST+1][FONT_H] = {\n")
        for code, rows in rows_all:
            gl = "\\\\" if code == 0x5c else chr(code)
            o.write("  {" + ",".join("0x%02x" % b for b in rows) + "}, /* 0x%02x '%s' */\n" % (code, gl))
        o.write("};\n")
    print("font8x16.h : %d glyphes (taille %d)" % (len(rows_all), size))

if __name__ == "__main__":
    main()
