#!/usr/bin/env python3
import argparse
import base64
import os
import time
import serial
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument("port")
args = parser.parse_args()

PALETTE = [
    (0x00, 0x00, 0x00), # 0
    (0x0B, 0x1A, 0x09), # 1
    (0x16, 0x30, 0x0F), # 2
    (0x24, 0x50, 0x1C), # 3
    (0x37, 0x6F, 0x2A), # 4
    (0x4F, 0x9A, 0x3C), # 5
    (0x6C, 0xC4, 0x56), # 6
    (0x85, 0xE5, 0x70), # 7
    (0xB4, 0xF3, 0xA2), # 8
    (0xDA, 0xFD, 0xD1), # 9
    (0, 0, 0), (0, 0, 0), (0, 0, 0), (0, 0, 0), (0, 0, 0), (0, 0, 0)
]

ser = serial.Serial()
ser.port = args.port
ser.baudrate = 115200
ser.dtr = False
ser.rts = False
ser.timeout = 10

ser.open()
ser.reset_input_buffer()

time.sleep(0.1)

print("Sending GALLERY...")
ser.write(b"GALLERY\n")
ser.flush()

# Wait for acknowledgment instead of hardcoded sleep
start_wait = time.time()
while time.time() - start_wait < 2:
    line = ser.readline().decode("ascii", errors="ignore").strip()
    if line == "OK:GALLERY":
        break

print("Sending SNAP...")
ser.write(b"SNAP\n")
ser.flush()

lines = []
in_snap = False
start_time = time.time()

while time.time() - start_time < 10:
    line = ser.readline().decode("ascii", errors="ignore").strip()
    if line.startswith("OK:"):
        print(f"Device says: {line}")
    if line == "SNAP_BEGIN":
        in_snap = True
        continue
    if line == "SNAP_END":
        break
    if in_snap and line:
        lines.append(line)

ser.close()

if not lines:
    print("No SNAP_BEGIN received.")
    exit(1)

data = base64.b64decode("".join(lines))
if len(data) != 320 * 240 // 2:
    print(f"invalid snapshot size: {len(data)}")
    exit(1)

img = Image.new("RGB", (320, 240))
pixels = img.load()

idx = 0
for y in range(240):
    for x in range(0, 320, 2):
        b = data[idx]
        idx += 1
        p1 = (b >> 4) & 0x0F
        p2 = b & 0x0F
        pixels[x, y] = PALETTE[p1]
        pixels[x+1, y] = PALETTE[p2]

# Nearest neighbor 2x
img = img.resize((640, 480), Image.NEAREST)
os.makedirs("snaps", exist_ok=True)

# find next frame number
frame_idx = 0
while os.path.exists(f"snaps/frame_{frame_idx:03d}.png"):
    frame_idx += 1

out_path = f"snaps/frame_{frame_idx:03d}.png"
img.save(out_path)
print(out_path)
