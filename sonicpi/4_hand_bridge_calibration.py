# Reads up to four sensor IDs from the receiver ESP32 and sends movement,
# flicks, and a rough relative displacement estimate to Sonic Pi.
#
# OSC:
#   /move         <sensor_id> <energy>
#   /flick        <sensor_id> <strength>
#   /displacement <sensor_id> <x_m> <y_m> <z_m>
#   /az           <sensor_id> <z_acceleration_minus_calibration_gravity_m_s2>
#
# Each sensor calibrates when it first appears. Hold it still in its starting
# pose for about two seconds; that pose is displacement (0, 0, 0).

import math
import time

import serial
from pythonosc.udp_client import SimpleUDPClient

PORT = "/dev/ttyUSB0"
GYRO_FULL = 8.0
ACCEL_FULL = 15.0
SMOOTH = 0.3
FLICK_ON = 0.5
FLICK_OFF = 0.2
CALIBRATION_SAMPLES = 100  # ~2 seconds at 50 Hz
SEND_INTERVAL = 0.05
AZ_SEND_INTERVAL = 0.02  # preserve short acceleration reversals at ~50 Hz

# Map displacement into the normalized controls used by bass_glove.rb.
# Adjust axes/signs to match how each MPU is mounted. Values saturate at +/-1.
Z_AXIS = 2
Z_SIGN = 1.0
X_AXIS = 0
X_SIGN = 1.0
CONTROL_RANGE_M = 0.25

# Double integration of a low-cost IMU drifts. Damping limits runaway estimates;
# this is a short-term relative estimate, not a measured absolute position.
VELOCITY_DAMPING = 0.98
STATIONARY_ACCEL = 0.35  # m/s^2 after removing calibrated gravity vector
STATIONARY_GYRO = 0.12   # rad/s after removing gyro bias

sonic_pi = SimpleUDPClient("127.0.0.1", 4560)
receiver = serial.Serial(PORT, 115200, timeout=1)

calibration = {}
state = {}
last_print = 0.0

print("Four-sensor bridge running (IDs 1-4). Move! Ctrl+C to stop.\n")
print(f"Reading serial from {PORT} at 115200; close Arduino Serial Monitor first.")
ignored_lines = 0

while True:
    line = receiver.readline().decode(errors="ignore").strip()
    parts = line.split(",")
    if len(parts) != 8:
        if line and ignored_lines < 5:
            print(f"\nIgnoring non-sensor serial line: {line!r}")
            ignored_lines += 1
        continue
    try:
        sensor = int(parts[0])
        timestamp_ms = int(parts[1])
        ax, ay, az, gx, gy, gz = [float(value) for value in parts[2:]]
    except ValueError:
        continue
    if sensor not in (1, 2, 3, 4):
        if ignored_lines < 5:
            print(f"\nIgnoring sensor ID {sensor}; expected 1-4")
            ignored_lines += 1
        continue

    now = time.monotonic()
    if sensor not in state:
        state[sensor] = {
            "energy": 0.0, "ready_for_flick": True, "last_send": 0.0,
            "last_az_send": 0.0,
            "flicks": 0, "gravity": 0.0, "gyro_offset": [0.0] * 3,
            "gravity_vector": [0.0] * 3, "position": [0.0] * 3,
            "velocity": [0.0] * 3, "last_time": None,
            "last_print_energy": 0.0,
        }
        calibration[sensor] = []
        print(f"\nSensor {sensor} connected - hold it still in its zero pose for ~2 seconds...")

    readings = calibration[sensor]
    if len(readings) < CALIBRATION_SAMPLES:
        readings.append((ax, ay, az, gx, gy, gz))
        if len(readings) == CALIBRATION_SAMPLES:
            s = state[sensor]
            s["gravity"] = sum(math.sqrt(r[0]**2 + r[1]**2 + r[2]**2) for r in readings) / len(readings)
            s["gravity_vector"] = [sum(r[i] for r in readings) / len(readings) for i in range(3)]
            s["gyro_offset"] = [sum(r[i] for r in readings) / len(readings) for i in (3, 4, 5)]
            print(f"\nSensor {sensor} calibrated (gravity {s['gravity']:.2f} m/s^2; displacement zeroed)")
        continue

    s = state[sensor]
    corrected_gyro = [gx - s["gyro_offset"][0], gy - s["gyro_offset"][1], gz - s["gyro_offset"][2]]
    gyro_magnitude = math.sqrt(sum(v * v for v in corrected_gyro))
    acceleration = [ax - s["gravity_vector"][0], ay - s["gravity_vector"][1], az - s["gravity_vector"][2]]
    accel_magnitude = math.sqrt(sum(v * v for v in acceleration))

    turning = gyro_magnitude / GYRO_FULL
    total_accel = math.sqrt(ax * ax + ay * ay + az * az)
    pushing = abs(total_accel - s["gravity"]) / ACCEL_FULL
    raw = min(turning + pushing, 1.0)
    s["energy"] += SMOOTH * (raw - s["energy"])

    # Integrate acceleration relative to the calibrated starting pose. The
    # resulting vector is approximate and will drift during extended motion.
    previous_ms = s["last_time"]
    if previous_ms is not None:
        dt = (timestamp_ms - previous_ms) / 1000.0
        if 0.0 < dt <= 0.1:
            stationary = accel_magnitude < STATIONARY_ACCEL and gyro_magnitude < STATIONARY_GYRO
            for axis in range(3):
                if stationary:
                    s["velocity"][axis] = 0.0
                else:
                    s["velocity"][axis] = (s["velocity"][axis] + acceleration[axis] * dt) * VELOCITY_DAMPING
                    s["position"][axis] += s["velocity"][axis] * dt
    s["last_time"] = timestamp_ms

    if now - s["last_send"] >= SEND_INTERVAL:
        sonic_pi.send_message("/move", [sensor, s["energy"]])
        sonic_pi.send_message("/displacement", [sensor, *s["position"]])
        z = max(-1.0, min(1.0, Z_SIGN * s["position"][Z_AXIS] / CONTROL_RANGE_M))
        x = max(-1.0, min(1.0, X_SIGN * s["position"][X_AXIS] / CONTROL_RANGE_M))
        sonic_pi.send_message("/z", [sensor, z])
        sonic_pi.send_message("/x", [sensor, x])
        s["last_send"] = now

    # Keep the rapid Z acceleration sign change intact for gesture detection.
    if now - s["last_az_send"] >= AZ_SEND_INTERVAL:
        sonic_pi.send_message("/az", [sensor, acceleration[Z_AXIS]])
        s["last_az_send"] = now

    if s["ready_for_flick"] and s["energy"] > FLICK_ON:
        s["flicks"] += 1
        sonic_pi.send_message("/flick", [sensor, s["energy"]])
        s["ready_for_flick"] = False
    elif not s["ready_for_flick"] and s["energy"] < FLICK_OFF:
        s["ready_for_flick"] = True

    if now - last_print > 0.1:
        last_print = now
        meters = ""
        for client_id in sorted(state):
            client = state[client_id]
            bar = "#" * int(client["energy"] * 20)
            pos = client["position"]
            meters += (f"  [{client_id}] {client['energy']:4.2f} |{bar:<20}| "
                       f"d=({pos[0]:+.2f},{pos[1]:+.2f},{pos[2]:+.2f})m "
                       f"flicks {client['flicks']:<3}")
        print("\r" + meters, end="")
