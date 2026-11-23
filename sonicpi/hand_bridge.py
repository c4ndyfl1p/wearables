# hand_bridge.py — reads the hand sensor data from the receiver ESP32 (USB)
# and sends movement to Sonic Pi.
#
# Sends to Sonic Pi:
#   /move   <energy>     how much the hand is moving: 0 = still, 1 = very fast (20x per second)
#   /flick  <strength>   a quick, energetic movement (a flick, swing or punch)
#
# Run:  ~/gyroenv/bin/python hand_bridge.py
# (Close the Arduino Serial Monitor first.)

import math
import time

import serial
from pythonosc.udp_client import SimpleUDPClient

# ================= SETTINGS =================
PORT = "/dev/ttyUSB0"   # the receiver's port
GYRO_FULL = 8.0         # rotation (rad/s) that counts as full energy (8 rad/s = about 460 deg/s)
ACCEL_FULL = 15.0       # push (m/s2 beyond gravity) that counts as full energy
SMOOTH = 0.3            # 0-1: lower = smoother but slower to react
FLICK_ON = 0.5          # energy above this = a flick
FLICK_OFF = 0.2         # energy must drop below this before the next flick can happen
GRAVITY = 9.81          # what the accelerometer reads when the hand is still

sonic_pi = SimpleUDPClient("127.0.0.1", 4560)
receiver = serial.Serial(PORT, 115200, timeout=1)

energy = 0.0
ready_for_flick = True
last_send = 0.0
flick_count = 0

print("Hand bridge running. Move your hand! (Ctrl+C to stop)\n")

while True:
    line = receiver.readline().decode(errors="ignore").strip()

    # A data line looks like: time,ax,ay,az,gx,gy,gz
    parts = line.split(",")
    if len(parts) != 7:
        continue
    try:
        ax, ay, az, gx, gy, gz = [float(x) for x in parts[1:]]
    except ValueError:
        continue

    # Turning: total rotation speed around all three axes
    turning = math.sqrt(gx * gx + gy * gy + gz * gz) / GYRO_FULL

    # Pushing: total acceleration minus gravity (works no matter how the hand is tilted)
    total_accel = math.sqrt(ax * ax + ay * ay + az * az)
    pushing = abs(total_accel - GRAVITY) / ACCEL_FULL

    # Combine both, limit to 0..1, and smooth it
    raw = min(turning + pushing, 1.0)
    energy = energy + SMOOTH * (raw - energy)

    # Send the movement energy 20 times per second, and show a meter in the terminal
    now = time.time()
    if now - last_send > 0.05:
        sonic_pi.send_message("/move", energy)
        last_send = now
        bar = "#" * int(energy * 40)
        print(f"\rmovement {energy:4.2f} |{bar:<40}|  flicks: {flick_count}", end="")

    # Flick detection: energy jumps above FLICK_ON, then must calm down before the next one
    if ready_for_flick and energy > FLICK_ON:
        flick_count += 1
        sonic_pi.send_message("/flick", energy)
        ready_for_flick = False
    if not ready_for_flick and energy < FLICK_OFF:
        ready_for_flick = True
