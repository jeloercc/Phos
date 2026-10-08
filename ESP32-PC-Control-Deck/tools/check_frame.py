#!/usr/bin/env python3
import argparse
import base64
import pathlib
import serial
import struct
import time
import zlib
import re

parser = argparse.ArgumentParser()
parser.add_argument("--port", required=True)
parser.add_argument("--mode", choices=("CLOSE", "DRIFT"), default="CLOSE")
parser.add_argument("--count", type=int, default=3)
parser.add_argument("--prefix", default="/tmp/phos-check")
args = parser.parse_args()

PALETTE = [
    (0, 0, 0), (11, 26, 9), (22, 48, 15), (36, 80, 28),
    (55, 111, 42), (79, 154, 60), (108, 196, 86), (133, 229, 112),
    (180, 243, 162), (218, 253, 209),
] + [(0, 0, 0)] * 6

def png(path, pixels):
    raw = bytearray()
    for y in range(240):
        raw.append(0)
        for x in range(320):
            raw.extend(PALETTE[pixels[y * 320 + x]])
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))
    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", struct.pack(">IIBBBBB", 320, 240, 8, 2, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    data += chunk(b"IEND", b"")
    pathlib.Path(path).write_bytes(data)

def snapshot(device, mode):
    device.write(("IDLE\n" + mode + "\nSNAP\n").encode())
    device.flush()
    encoded = []
    active = False
    deadline = time.time() + 12
    while time.time() < deadline:
        line = device.readline().decode("ascii", errors="ignore").strip()
        if line == "SNAP_BEGIN":
            active = True
        elif line == "SNAP_END":
            break
        elif active and line:
            encoded.append("".join(re.findall(r"[A-Za-z0-9+/=]", line)))
    data = base64.b64decode("".join(encoded))
    if len(data) != 38400:
        raise RuntimeError(f"invalid snapshot size: {len(data)}")
    pixels = []
    for byte in data:
        pixels.extend((byte >> 4, byte & 0x0F))
    return pixels

def blobs(pixels):
    seen, result = set(), []
    for start, value in enumerate(pixels):
        if value not in (6, 7, 8, 9) or start in seen:
            continue
        stack, points = [start], []
        seen.add(start)
        while stack:
            point = stack.pop()
            x, y = point % 320, point // 320
            points.append((x, y))
            for neighbor in (point - 1, point + 1, point - 320, point + 320):
                if (0 <= neighbor < len(pixels) and
                    pixels[neighbor] in (6, 7, 8, 9) and neighbor not in seen and
                    abs(neighbor % 320 - x) + abs(neighbor // 320 - y) == 1):
                    seen.add(neighbor)
                    stack.append(neighbor)
        if len(points) >= 300:
            xs, ys = zip(*points)
            result.append((min(xs), min(ys), max(xs), max(ys), len(points)))
    return result

def analyze(pixels, path):
    used = set(pixels)
    cells = [
        any(pixels[(row * 10 + yy) * 320 + col * 8 + xx] > 0
            for yy in range(10) for xx in range(8))
        for row in range(24) for col in range(40)
    ]
    eyes = blobs(pixels)
    centers = [((b[0] + b[2]) / 2, (b[1] + b[3]) / 2) for b in eyes]
    if len(centers) != 2:
        centers = [(130, 120), (190, 120)]
    row_span = range(max(0, int(min(y for _, y in centers) - 20) // 10),
                     min(24, int(max(y for _, y in centers) + 20) // 10 + 1))
    band = [cells[r * 40 + c] for r in row_span for c in range(40)
            if all(abs(c * 8 + 4 - x) > 60 for x, _ in centers)]
    rest = [cells[r * 40 + c] for r in range(24) if r not in row_span for c in range(40)]
    band_density = sum(band) / max(1, len(band))
    rest_density = sum(rest) / max(1, len(rest))
    edge_rows = []
    for row in range(24):
        edge_rows.append(sum(cells[row * 40 + col] and
                             max(pixels[(row * 10 + yy) * 320 + col * 8 + xx]
                                 for yy in range(10) for xx in range(8)) in (1, 9)
                             for col in range(40)))
    lit_columns = sum(any(cells[row * 40 + col] for row in range(24)) for col in range(40))
    between = any(pixels[y * 320 + x] > 0 for y in range(240) for x in range(320)
                  if centers[0][0] + 13 < x < centers[1][0] - 13)
    above_below = all(any(pixels[y * 320 + x] > 0 for y in range(240) for x in range(320)
                          if abs(x - cx) < 20 and abs(y - cy) > 50)
                      for cx, cy in centers)
    eye_area = sum(pixels[y * 320 + x] in (6, 7, 8, 9)
                   for y in range(240) for x in range(320))
    core_area = sum(pixels[y * 320 + x] in (8, 9)
                    for y in range(240) for x in range(320))
    ratio = core_area / max(1, eye_area)
    checks = {
        "INDICES_0_9": max(used, default=0) <= 9,
        "RAIN_COLUMNS": lit_columns >= 25,
        "RAIN_ROWS": sum(any(cells[row * 40 + col] for col in range(40)) for row in range(24)) >= 18,
        "EYES_TWO_BLOBS": len(eyes) == 2,
        "BLACK_RATIO": pixels.count(0) / len(pixels) >= 0.50,
        "BAND": band_density >= rest_density * 0.60,
        "EDGE_LINE": max(edge_rows) <= lit_columns * 0.25,
        "BETWEEN_EYES": between,
        "ABOVE_BELOW": above_below,
        "AREA_8_9": 0.15 <= ratio <= 0.75,
    }
    for name, passed in checks.items():
        print(f"{'PASS' if passed else 'FAIL'} {name}")
    print(f"INFO band_density={band_density:.3f} rest_density={rest_density:.3f} "
          f"ratio={band_density / max(rest_density, .001):.3f} max_edge_row={max(edge_rows)} "
          f"lit_columns={lit_columns} between={between} above_below={above_below} "
          f"area_8_9={core_area}/{eye_area}={ratio:.3f} png={path}")
    return all(checks.values())

with serial.Serial(args.port, 115200, timeout=2) as device:
    device.reset_input_buffer()
    time.sleep(2.0)
    snapshot(device, args.mode)
    time.sleep(1.0)
    passed = True
    for index in range(args.count):
        raw = pathlib.Path(f"{args.prefix}-{args.mode.lower()}-{index}.raw")
        pixels = snapshot(device, args.mode)
        raw.write_bytes(bytes((pixels[i] << 4) | pixels[i + 1] for i in range(0, len(pixels), 2)))
        image = raw.with_suffix(".png")
        png(image, pixels)
        passed = analyze(pixels, image) and passed
        if index + 1 < args.count:
            time.sleep(1)
if not passed:
    raise SystemExit(1)
