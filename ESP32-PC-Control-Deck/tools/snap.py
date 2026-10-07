#!/usr/bin/env python3
import argparse
import binascii
import pathlib
import serial

parser = argparse.ArgumentParser()
parser.add_argument("--port", required=True)
parser.add_argument("--output", required=True)
args = parser.parse_args()

with serial.Serial(args.port, 115200, timeout=2) as device:
    device.reset_input_buffer()
    device.write(b"SNAP\n")
    device.flush()
    lines = []
    while True:
        line = device.readline().decode("ascii", errors="ignore").strip()
        if line == "SNAP_BEGIN":
            break
    while True:
        line = device.readline().decode("ascii", errors="ignore").strip()
        if line == "SNAP_END":
            break
        if line:
            lines.append(line)

data = binascii.unhexlify("".join(lines))
if len(data) != 320 * 240 // 2:
    raise SystemExit(f"invalid snapshot size: {len(data)}")
pathlib.Path(args.output).write_bytes(data)
print(args.output)
