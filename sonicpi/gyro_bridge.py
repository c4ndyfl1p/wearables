# gyro_bridge.py — reads the stick data from the receiver ESP32 (USB)
# and sends it to Sonic Pi.
#
# Sends to Sonic Pi:
#   /gyro  <speed>       spin speed in degrees per second (+ or -), 20 times a second
#   /spin  <direction>   every full spin: 1 or -1
#
# Install once:  pip install pyserial python-osc
# Run:           python gyro_bridge.py
# (Close the Arduino Serial Monitor first - only one program can use the port.)

import math
import time

import serial
from pythonosc.udp_client import SimpleUDPClient

# ================= SETTINGS =================
PORT = "/dev/ttyUSB0"       # the receiver's port (check with: ls /dev/ | grep tty)
SPIN_AXIS = 2               # gyro axis the stick spins around: 0 = x, 1 = y, 2 = z
DEAD_ZONE = 0.5             # rad/s: slower rotation is ignored as noise
FULL_SPIN = 2 * math.pi     # one full spin in radians

sonic_pi = SimpleUDPClient("127.0.0.1", 4560)   # Sonic Pi on this computer
receiver = serial.Serial(PORT, 115200, timeout=1)

angle = 0.0
last_time = None
last_send = 0.0
spin_count = 0

print("Bridge running. Spin the stick! (Ctrl+C to stop)")

while True:
    line = receiver.readline().decode(errors="ignore").strip()

    # A data line looks like: time,ax,ay,az,gx,gy,gz
    parts = line.split(",")
    if len(parts) != 7:
        continue                # not a data line (e.g. "Receiver ready"): skip it
    try:
        t = int(parts[0])
        gyro = [float(parts[4]), float(parts[5]), float(parts[6])]
    except ValueError:
        continue

    rate = gyro[SPIN_AXIS]      # rad/s around the spin axis

    # Send the spin speed to Sonic Pi, 20 times per second
    now = time.time()
    if now - last_send > 0.05:
        sonic_pi.send_message("/gyro", rate * 57.3)   # 57.3 converts rad/s to deg/s
        last_send = now

    # ---- spin detection (same idea as the Arduino version) ----
    if last_time is None or t < last_time or t - last_time > 500:
        last_time = t           # first message, restart, or long gap
        continue
    seconds = (t - last_time) / 1000
    last_time = t

    if abs(rate) < DEAD_ZONE:
        rate = 0
    if rate * angle < 0:
        angle = 0               # direction changed: start counting fresh

    angle += rate * seconds

    if angle >= FULL_SPIN:
        angle -= FULL_SPIN
        spin_count += 1
        print(f"spin detected (+)   total: {spin_count}")
        sonic_pi.send_message("/spin", 1)
    elif angle <= -FULL_SPIN:
        angle += FULL_SPIN
        spin_count += 1
        print(f"spin detected (-)   total: {spin_count}")
        sonic_pi.send_message("/spin", -1)
