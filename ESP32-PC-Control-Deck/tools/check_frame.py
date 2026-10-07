#!/usr/bin/env python3
import argparse
import pathlib
import serial
import struct
import subprocess
import time
import zlib

parser = argparse.ArgumentParser()
parser.add_argument("--port", required=True)
parser.add_argument("--frame", default="/tmp/phos-frame.raw")
parser.add_argument("--count", type=int, default=3)
args = parser.parse_args()

palette = [
    (0x00, 0x00, 0x00), (0x0B, 0x1A, 0x09), (0x16, 0x30, 0x0F),
    (0x24, 0x50, 0x1C), (0x37, 0x6F, 0x2A), (0x4F, 0x9A, 0x3C),
    (0x6C, 0xC4, 0x56), (0x85, 0xE5, 0x70), (0xB4, 0xF3, 0xA2),
    (0xDA, 0xFD, 0xD1),
]

def write_png(path, pixels):
    raw = bytearray()
    for y in range(240):
        raw.append(0)
        for x in range(320):
            raw.extend(palette[pixels[y * 320 + x]])
    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", 320, 240, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    pathlib.Path(path).write_bytes(png)

def capture(path):
    subprocess.run([
        "python3", str(pathlib.Path(__file__).with_name("snap.py")),
        "--port", args.port, "--output", str(path),
    ], check=True)
    data = pathlib.Path(path).read_bytes()
    pixels = []
    for byte in data:
        pixels.extend((byte >> 4, byte & 0x0F))
    write_png(pathlib.Path(path).with_suffix(".png"), pixels)
    return pixels

def blobs(pixels):
    seen = set()
    result = []
    for start, value in enumerate(pixels):
        if value not in (8, 9) or start in seen:
            continue
        stack = [start]
        seen.add(start)
        points = []
        while stack:
            point = stack.pop()
            x, y = point % 320, point // 320
            points.append((x, y))
            for neighbor in (point - 1, point + 1, point - 320, point + 320):
                if 0 <= neighbor < len(pixels) and pixels[neighbor] in (8, 9) and neighbor not in seen:
                    if abs(neighbor % 320 - x) + abs(neighbor // 320 - y) == 1:
                        seen.add(neighbor)
                        stack.append(neighbor)
        if len(points) >= 300:
            xs, ys = zip(*points)
            result.append((min(xs), min(ys), max(xs), max(ys), len(points)))
    return result

between_seen = False

def analyze(pixels, frame_path):
    global between_seen
    used = set(pixels)
    columns = sum(any(pixels[y * 320 + x] > 0 for y in range(240))
                  for x in range(0, 320, 8))
    rows = sum(any(pixels[y * 320 + x] > 0 for x in range(320))
               for y in range(0, 240, 10))
    black_ratio = pixels.count(0) / len(pixels)
    eye_blobs = blobs(pixels)
    centers = [((blob[0] + blob[2]) / 2, (blob[1] + blob[3]) / 2,
                (blob[3] - blob[1] + 1) / 2) for blob in eye_blobs]
    if len(centers) != 2:
        centers = [(130, 120, 17), (190, 120, 17)]
    lit_cells = []
    for row in range(24):
        for col in range(40):
            cell = pixels[row * 10 * 320 + col * 8]
            lit = any(pixels[(row * 10 + yy) * 320 + col * 8 + xx] > 0
                      for yy in range(10) for xx in range(8))
            lit_cells.append(lit)
    eye_top = int(min(center[1] for center in centers)) - 20
    eye_bottom = int(max(center[1] for center in centers)) + 20
    band_rows = range(max(0, eye_top // 10), min(24, eye_bottom // 10 + 1))
    far_band = []
    rest = []
    for row in range(24):
        for col in range(40):
            px = col * 8 + 4
            far = all(abs(px - center[0]) > 60 for center in centers)
            if row in band_rows and far:
                far_band.append(lit_cells[row * 40 + col])
            elif row not in band_rows:
                rest.append(lit_cells[row * 40 + col])
    band_density = sum(far_band) / max(1, len(far_band))
    rest_density = sum(rest) / max(1, len(rest))
    band_ok = band_density >= rest_density * 0.60
    row_counts = []
    for row in range(24):
        ends = 0
        for col in range(40):
            here = lit_cells[row * 40 + col]
            values = [pixels[(row * 10 + yy) * 320 + col * 8 + xx]
                      for yy in range(10) for xx in range(8)]
            peak = max(values)
            if here and peak in (1, 9):
                ends += 1
        row_counts.append(ends)
    lit_columns = max(1, sum(any(lit_cells[row * 40 + col] for row in range(24)) for col in range(40)))
    edge_limit = lit_columns * 0.25
    edge_ok = max(row_counts) <= edge_limit
    between = any(pixels[row * 10 * 320 + y * 320 + x] > 0
                  for row in range(24) for y in range(10)
                  for x in range(320)
                  if centers[0][0] + 13 < x < centers[1][0] - 13)
    between_seen = between_seen or between
    above_below = all(any(lit_cells[row * 40 + col]
                          for row in range(24) for col in range(40)
                          if abs(col * 8 + 4 - center[0]) < 20 and
                          abs(row * 10 + 5 - center[1]) > center[2] + 34)
                      for center in centers)
    checks = {
        "INDICES_0_9": max(used, default=0) <= 9,
        "RAIN_COLUMNS": columns >= 25,
        "RAIN_ROWS": rows >= 18,
        "EYES_TWO_BLOBS": len(eye_blobs) == 2 and all(
            18 <= blob[2] - blob[0] + 1 <= 34 and
            24 <= blob[3] - blob[1] + 1 <= 44 for blob in eye_blobs),
        "BLACK_RATIO": black_ratio >= 0.50,
        "BAND": band_ok,
        "EDGE_LINE": edge_ok,
        "BETWEEN_EYES": between_seen,
        "ABOVE_BELOW": above_below,
    }
    for name, passed in checks.items():
        print(f"{'PASS' if passed else 'FAIL'} {name}")
    print(f"INFO band_density={band_density:.3f} rest_density={rest_density:.3f} "
          f"ratio={band_density / max(rest_density, 0.001):.3f} "
          f"max_row={max(row_counts)} lit_columns={lit_columns} "
          f"used={sorted(used)} columns={columns} rows={rows} black={black_ratio:.3f} "
          f"eye_blobs={eye_blobs} "
          f"between={between} above_below={above_below} png={pathlib.Path(frame_path).with_suffix('.png')}")
    return all(checks.values())

with serial.Serial(args.port, 115200, timeout=2) as device:
    device.reset_input_buffer()
    device.write(b"PAL\n")
    device.flush()
    palette_ok = any(device.readline().decode(errors="ignore").strip() == "PALETTE_OK"
                     for _ in range(12))
print(f"{'PASS' if palette_ok else 'FAIL'} PALETTE")

passed = palette_ok
for index in range(args.count):
    frame_path = pathlib.Path(args.frame).with_name(
        pathlib.Path(args.frame).stem + f"_{index}.raw")
    pixels = capture(frame_path)
    passed = analyze(pixels, frame_path) and passed
    if index + 1 < args.count:
        time.sleep(1.0)
if not passed:
    raise SystemExit(1)
