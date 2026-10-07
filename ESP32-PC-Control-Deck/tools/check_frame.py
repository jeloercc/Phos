#!/usr/bin/env python3
import argparse
import pathlib
import serial
import struct
import subprocess
import zlib

parser = argparse.ArgumentParser()
parser.add_argument("--port", required=True)
parser.add_argument("--frame", default="/tmp/phos-frame.raw")
args = parser.parse_args()

subprocess.run([
    "python3", str(pathlib.Path(__file__).with_name("snap.py")),
    "--port", args.port, "--output", args.frame,
], check=True)

palette_ok = False
with serial.Serial(args.port, 115200, timeout=2) as device:
    device.reset_input_buffer()
    device.write(b"PAL\n")
    device.flush()
    for _ in range(12):
        line = device.readline().decode("ascii", errors="ignore").strip()
        if line == "PALETTE_OK":
            palette_ok = True
            break

data = pathlib.Path(args.frame).read_bytes()
pixels = []
for byte in data:
    pixels.extend((byte >> 4, byte & 0x0F))

used = set(pixels)
column_count = sum(any(pixels[y * 320 + x] > 0 for y in range(240)) for x in range(0, 320, 8))
row_count = sum(any(pixels[y * 320 + x] > 0 for x in range(320)) for y in range(0, 240, 10))
zero_ratio = pixels.count(0) / len(pixels)
eye_pixels = {index for index in (8, 9) if index in used}

eye_set = {8, 9}
seen = set()
blobs = []
for start, value in enumerate(pixels):
    if value not in eye_set or start in seen:
        continue
    stack = [start]
    seen.add(start)
    points = []
    while stack:
        point = stack.pop()
        x = point % 320
        y = point // 320
        points.append((x, y))
        for neighbor in (point - 1, point + 1, point - 320, point + 320):
            if 0 <= neighbor < len(pixels) and pixels[neighbor] in eye_set and neighbor not in seen:
                if abs((neighbor % 320) - x) + abs((neighbor // 320) - y) == 1:
                    seen.add(neighbor)
                    stack.append(neighbor)
    if len(points) >= 20:
        xs = [point[0] for point in points]
        ys = [point[1] for point in points]
        blobs.append((min(xs), min(ys), max(xs), max(ys), len(points)))

def write_png(path):
    palette = [
        (0x00, 0x00, 0x00), (0x0B, 0x1A, 0x09), (0x16, 0x30, 0x0F),
        (0x24, 0x50, 0x1C), (0x37, 0x6F, 0x2A), (0x4F, 0x9A, 0x3C),
        (0x6C, 0xC4, 0x56), (0x85, 0xE5, 0x70), (0xB4, 0xF3, 0xA2),
        (0xDA, 0xFD, 0xD1)]
    raw = bytearray()
    for y in range(240):
        raw.append(0)
        for x in range(320):
            raw.extend(palette[pixels[y * 320 + x]])
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", 320, 240, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    pathlib.Path(path).write_bytes(png)

png_path = pathlib.Path(args.frame).with_suffix(".png")
write_png(png_path)
eye_blobs = [blob for blob in blobs if blob[4] >= 300]
blob_ok = len(eye_blobs) == 2 and all(
    18 <= blob[2] - blob[0] + 1 <= 34 and 24 <= blob[3] - blob[1] + 1 <= 44
    for blob in eye_blobs
)
checks = {
    "palette": palette_ok,
    "indices_0_9": max(used, default=0) <= 9,
    "rain_columns": column_count >= 25,
    "rain_rows": row_count >= 18,
    "eyes_two_blobs": blob_ok,
    "black_ratio": zero_ratio >= 0.50,
}
for name, passed in checks.items():
    print(f"{'PASS' if passed else 'FAIL'} {name}")
print(f"INFO used={sorted(used)} columns={column_count} rows={row_count} black={zero_ratio:.3f}")
print(f"INFO blobs={blobs} eye_blobs={eye_blobs} png={png_path}")
if not all(checks.values()):
    raise SystemExit(1)
