import serial
import time

try:
    s = serial.Serial('/dev/cu.usbserial-1440', 115200, timeout=1)
    time.sleep(2)
    s.write(b"DEBUG ON\n")
    time.sleep(0.1)
    s.write(b"GALLERY\n")
    print("Sent DEBUG ON and GALLERY")
    s.close()
except Exception as e:
    print(e)
