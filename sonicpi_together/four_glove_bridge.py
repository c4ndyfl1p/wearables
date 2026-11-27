# four_glove_bridge.py — reads all four gloves from the receiver ESP32 and
# sends their movement to Sonic Pi.
#
#   IDs 1 and 2 (synth gloves):  /flick
#   ID 3 (didgeridoo):           /didgeridoo_motion_xz
#   ID 4 (handpan):              /handpan_motion_xz
#   Optional streams (/move, /displacement, /z, /x, /az, /accel_xz) are off by
#   default to keep Sonic Pi from falling behind; switch them on in SETTINGS.
#
# OSC messages:
#   /move                 <id> <energy>                 all gloves
#   /flick                <id> <strength>               all gloves
#   /displacement         <id> <x_m> <y_m> <z_m>        IDs 3-4
#   /z, /x                <id> <-1..1>                  IDs 3-4
#   /az                   <id> <z_accel_m_s2>           IDs 3-4
#   /accel_xz             <id> <x_accel> <z_accel>      IDs 3-4
#   /didgeridoo_motion_xz <id> <x_accel> <z_accel>      ID 3
#   /handpan_motion_xz    <id> <x_accel> <z_accel>      ID 4
#
# Each glove calibrates when it first appears: hold it still for about 2 seconds.
#
# Run:  ~/gyroenv/bin/python four_glove_bridge.py
# (Close the Arduino Serial Monitor first.)

import math
import time

import serial
from pythonosc.udp_client import SimpleUDPClient

# ================= SETTINGS =================
PORT = "/dev/ttyUSB0"      # the receiver's port
BAUD = 230400              # must match Serial.begin(...) in the receiver sketch

GYRO_FULL = 8.0            # rotation (rad/s) that counts as full energy
ACCEL_FULL = 15.0          # push (m/s2 beyond gravity) that counts as full energy
SMOOTH = 0.3
FLICK_ON = 0.5
FLICK_OFF = 0.2
CALIBRATION_SAMPLES = 100  # about 2 seconds at 50 Hz
SEND_INTERVAL = 0.05       # /move, /displacement, /z, /x: 20 times per second
AZ_SEND_INTERVAL = 0.02    # acceleration streams: 50 times per second

# Which gloves exist, and whether each also sends the extra data streams
# (/displacement, /z, /x, /az, /accel_xz). all_gloves.rb doesn't use any of them,
# so they are switched off: every OSC message costs Sonic Pi time, and too many
# cause "thread got too far behind". Set a glove to True if another file needs them.
EXTRA_STREAMS = {1: False, 2: False, 3: False, 4: False}

# /move is also unused by all_gloves.rb; set to True for files that use it.
SEND_MOVE = False

# Named motion streams the Sonic Pi file listens for
MOTION_ADDRESS = {3: "/didgeridoo_motion_xz", 4: "/handpan_motion_xz"}

# Axis mapping for the displacement controls (adjust to how each sensor is mounted)
Z_AXIS = 2
Z_SIGN = 1.0
X_AXIS = 0
X_SIGN = 1.0
CONTROL_RANGE_M = 0.25

# Displacement from double integration drifts; damping keeps it short-term.
VELOCITY_DAMPING = 0.98
STATIONARY_ACCEL = 0.35    # m/s^2 after removing calibrated gravity
STATIONARY_GYRO = 0.12     # rad/s after removing gyro bias

# ================= SETUP =================
sonic_pi = SimpleUDPClient("127.0.0.1", 4560)
receiver = serial.Serial(PORT, BAUD, timeout=1)

calibration = {}
state = {}
last_print = 0.0
ignored_lines = 0

print(f"Four-glove bridge reading {PORT} at {BAUD} baud. Ctrl+C to stop.")
print("Gloves 1-2: synth.  Glove 3: didgeridoo.  Glove 4: handpan.\n")

while True:
    line = receiver.readline().decode(errors="ignore").strip()

    # A data line looks like: id,time,ax,ay,az,gx,gy,gz
    parts = line.split(",")
    if len(parts) != 8:
        if line and ignored_lines < 5:
            print(f"\nIgnoring non-sensor line: {line!r}")
            ignored_lines += 1
        continue
    try:
        sensor = int(parts[0])
        timestamp_ms = int(parts[1])
        ax, ay, az, gx, gy, gz = [float(v) for v in parts[2:]]
    except ValueError:
        continue
    if sensor not in EXTRA_STREAMS:
        if ignored_lines < 5:
            print(f"\nIgnoring sensor ID {sensor}; expected 1-4")
            ignored_lines += 1
        continue

    now = time.monotonic()

    # First message from a new glove: set up its values
    if sensor not in state:
        state[sensor] = {
            "energy": 0.0, "ready_for_flick": True,
            "last_send": 0.0, "last_az_send": 0.0, "flicks": 0,
            "gravity": 0.0, "gravity_vector": [0.0] * 3, "gyro_offset": [0.0] * 3,
            "position": [0.0] * 3, "velocity": [0.0] * 3, "last_time": None,
        }
        calibration[sensor] = []
        print(f"\nGlove {sensor} connected - hold it still for ~2 seconds...")

    # Calibration: collect resting readings, then work out this glove's "zero"
    readings = calibration[sensor]
    if len(readings) < CALIBRATION_SAMPLES:
        readings.append((ax, ay, az, gx, gy, gz))
        if len(readings) == CALIBRATION_SAMPLES:
            s = state[sensor]
            n = len(readings)
            s["gravity"] = sum(math.sqrt(r[0]**2 + r[1]**2 + r[2]**2) for r in readings) / n
            s["gravity_vector"] = [sum(r[i] for r in readings) / n for i in range(3)]
            s["gyro_offset"] = [sum(r[i] for r in readings) / n for i in (3, 4, 5)]
            print(f"\nGlove {sensor} calibrated (gravity reads {s['gravity']:.2f})")
        continue

    s = state[sensor]

    # Remove the resting offsets
    gyro = [gx - s["gyro_offset"][0], gy - s["gyro_offset"][1], gz - s["gyro_offset"][2]]
    gyro_magnitude = math.sqrt(sum(v * v for v in gyro))
    accel = [ax - s["gravity_vector"][0], ay - s["gravity_vector"][1], az - s["gravity_vector"][2]]
    accel_magnitude = math.sqrt(sum(v * v for v in accel))

    # Movement energy: turning (gyro) + pushing (accelerometer minus gravity)
    turning = gyro_magnitude / GYRO_FULL
    total_accel = math.sqrt(ax * ax + ay * ay + az * az)
    pushing = abs(total_accel - s["gravity"]) / ACCEL_FULL
    raw = min(turning + pushing, 1.0)
    s["energy"] += SMOOTH * (raw - s["energy"])

    extra = EXTRA_STREAMS[sensor]

    # Rough displacement (only needed for the gloves that use it)
    if extra:
        previous_ms = s["last_time"]
        if previous_ms is not None:
            dt = (timestamp_ms - previous_ms) / 1000.0
            if 0.0 < dt <= 0.1:
                still = accel_magnitude < STATIONARY_ACCEL and gyro_magnitude < STATIONARY_GYRO
                for i in range(3):
                    if still:
                        s["velocity"][i] = 0.0
                    else:
                        s["velocity"][i] = (s["velocity"][i] + accel[i] * dt) * VELOCITY_DAMPING
                        s["position"][i] += s["velocity"][i] * dt
        s["last_time"] = timestamp_ms

    # 20 times per second: energy (all gloves) and displacement controls (3-4)
    if now - s["last_send"] >= SEND_INTERVAL:
        if SEND_MOVE:
            sonic_pi.send_message("/move", [sensor, s["energy"]])
        if extra:
            sonic_pi.send_message("/displacement", [sensor, *s["position"]])
            z = max(-1.0, min(1.0, Z_SIGN * s["position"][Z_AXIS] / CONTROL_RANGE_M))
            x = max(-1.0, min(1.0, X_SIGN * s["position"][X_AXIS] / CONTROL_RANGE_M))
            sonic_pi.send_message("/z", [sensor, z])
            sonic_pi.send_message("/x", [sensor, x])
        s["last_send"] = now

    # 50 times per second: the motion stream for the didgeridoo (3) and handpan (4)
    if now - s["last_az_send"] >= AZ_SEND_INTERVAL:
        if sensor in MOTION_ADDRESS:
            sonic_pi.send_message(MOTION_ADDRESS[sensor], [sensor, accel[X_AXIS], accel[Z_AXIS]])
        if extra:
            sonic_pi.send_message("/az", [sensor, accel[Z_AXIS]])
            sonic_pi.send_message("/accel_xz", [sensor, accel[X_AXIS], accel[Z_AXIS]])
        s["last_az_send"] = now

    # Flick detection (all gloves)
    if s["ready_for_flick"] and s["energy"] > FLICK_ON:
        s["flicks"] += 1
        sonic_pi.send_message("/flick", [sensor, s["energy"]])
        s["ready_for_flick"] = False
    elif not s["ready_for_flick"] and s["energy"] < FLICK_OFF:
        s["ready_for_flick"] = True

    # Meter for all gloves, 10 times per second
    if now - last_print > 0.1:
        last_print = now
        meters = ""
        for gid in sorted(state):
            g = state[gid]
            bar = "#" * int(g["energy"] * 12)
            meters += f" [{gid}] {g['energy']:4.2f} |{bar:<12}| flicks {g['flicks']:<3}"
        print("\r" + meters, end="")
