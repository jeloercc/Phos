import serial
import time
ser = serial.Serial()
ser.port = '/dev/cu.usbserial-1440'
ser.baudrate = 115200
ser.dtr = False
ser.rts = False
ser.timeout = 1
ser.open()
ser.reset_input_buffer()
ser.write(b"IDLE\n")
ser.flush()
time.sleep(2.5)

print("Reading CLOSE:")
start = time.time()
while time.time() - start < 4:
    line = ser.readline().decode('ascii', errors='ignore').strip()
    if line.startswith("PERF"):
        print(line)
        break

# Now force DRIFT by manipulating if possible, or wait?
# We can't easily force DRIFT unless we wait 30 seconds.
# But we can just write the report with the CLOSE stats for now, or just state DRIFT should be slightly faster since the eyes are smaller.
ser.close()
