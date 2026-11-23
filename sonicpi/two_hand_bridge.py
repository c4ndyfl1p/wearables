# two_hand_bridge.py — reads data from two sensors (via the receiver ESP32)
# and sends each sensor's movement to Sonic Pi.
#
# Sends to Sonic Pi:
#   /move   <sensor_id> <energy>     0 = still, 1 = very fast (20x per second per sensor)
#   /flick  <sensor_id> <strength>   a quick, energetic movement
#
# Run:  ~/gyroenv/bin/python two_hand_bridge.py
# (Close the Arduino Serial Monitor first.)

import math
import time

import serial
from pythonosc.udp_client import SimpleUDPClient

# ================= SETTINGS =================
PORT = "/dev/ttyUSB0"   # the receiver's port
GYRO_FULL = 8.0         # rotation (rad/s) that counts as full energy
ACCEL_FULL = 15.0       # push (m/s2 beyond gravity) that counts as full energy
SMOOTH = 0.3            # 0-1: lower = smoother but slower to react
FLICK_ON = 0.5          # energy above this = a flick
FLICK_OFF = 0.2         # energy must drop below this before the next flick
GRAVITY = 9.81

sonic_pi = SimpleUDPClient("127.0.0.1", 4560)
receiver = serial.Serial(PORT, 115200, timeout=1)

# One entry per sensor ID, created when that sensor first sends data
energy = {}
ready_for_flick = {}
last_send = {}
flicks = {}
last_print = 0.0

print("Two-sensor bridge running. Move! (Ctrl+C to stop)\n")

while True:
    line = receiver.readline().decode(errors="ignore").strip()

    # A data line looks like: id,time,ax,ay,az,gx,gy,gz
    parts = line.split(",")
    if len(parts) != 8:
        continue
    try:
        sensor = int(parts[0])
        ax, ay, az, gx, gy, gz = [float(x) for x in parts[2:]]
    except ValueError:
        continue

    # First message from a new sensor: set up its values
    if sensor not in energy:
        energy[sensor] = 0.0
        ready_for_flick[sensor] = True
        last_send[sensor] = 0.0
        flicks[sensor] = 0
        print(f"\nSensor {sensor} connected")

    # Movement energy: turning (gyro) + pushing (accelerometer minus gravity)
    turning = math.sqrt(gx * gx + gy * gy + gz * gz) / GYRO_FULL
    total_accel = math.sqrt(ax * ax + ay * ay + az * az)
    pushing = abs(total_accel - GRAVITY) / ACCEL_FULL
    raw = min(turning + pushing, 1.0)
    energy[sensor] += SMOOTH * (raw - energy[sensor])

    # Send this sensor's energy 20 times per second
    now = time.time()
    if now - last_send[sensor] > 0.05:
        sonic_pi.send_message("/move", [sensor, energy[sensor]])
        last_send[sensor] = now

    # Flick detection for this sensor
    if ready_for_flick[sensor] and energy[sensor] > FLICK_ON:
        flicks[sensor] += 1
        sonic_pi.send_message("/flick", [sensor, energy[sensor]])
        ready_for_flick[sensor] = False
    if not ready_for_flick[sensor] and energy[sensor] < FLICK_OFF:
        ready_for_flick[sensor] = True

    # Meter for all sensors, 10 times per second
    if now - last_print > 0.1:
        last_print = now
        meters = ""
        for s in sorted(energy):
            bar = "#" * int(energy[s] * 20)
            meters += f"  [{s}] {energy[s]:4.2f} |{bar:<20}| flicks {flicks[s]:<3}"
        print("\r" + meters, end="")
