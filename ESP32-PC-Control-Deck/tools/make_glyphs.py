#!/usr/bin/env python3
"""Extract real 4x8 Misaki glyphs and mirror them into 8x8 PROGMEM data."""

import argparse
import pathlib
import subprocess

HEX = "0123456789ABCDEF"
KATAKANA_CODES = list(range(0xA1, 0xE0))


def pixels(path):
    raw = subprocess.check_output(["magick", str(path), "-depth", "8", "gray:-"])
    if len(raw) != 64 * 128:
        raise SystemExit("expected the official Misaki 4x8 PNG at 64x128 pixels")
    return raw


def glyph(raw, code):
    cell_x = (code & 0x0F) * 4
    cell_y = (code >> 4) * 8
    rows = []
    for y in range(8):
        bits = 0
        for x in range(4):
            if raw[(cell_y + y) * 64 + cell_x + x] < 128:
                bits |= 1 << (7 - x)
        rows.append(bits)
    return rows


def generate(source, output):
    raw = pixels(source)
    codes = KATAKANA_CODES + [ord(char) for char in HEX]
    rows = [glyph(raw, code) for code in codes]
    lines = [
        "#pragma once", "", "#include <Arduino.h>", "",
        "struct PhosGlyph { uint8_t bits[8]; };",
        f"constexpr uint8_t PHOS_GLYPH_COUNT = {len(rows)};",
        "static const PhosGlyph PHOS_GLYPHS[PHOS_GLYPH_COUNT] PROGMEM = {",
    ]
    for row in rows:
        lines.append("    {{" + ", ".join(f"0x{value:02X}" for value in row) + "}},")
    lines += ["};", ""]
    pathlib.Path(output).write_text("\n".join(lines), encoding="ascii")


parser = argparse.ArgumentParser()
parser.add_argument("source", help="official Misaki misaki_4x8.png")
parser.add_argument("--output", default="../src/phos_glyphs.h")
args = parser.parse_args()
generate(args.source, pathlib.Path(__file__).resolve().parent / args.output)
