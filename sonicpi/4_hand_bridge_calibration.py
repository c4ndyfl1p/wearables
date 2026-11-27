# Reads up to four sensor IDs from the receiver ESP32 and sends movement,
# flicks, and a rough relative displacement estimate to Sonic Pi.
#
# OSC:
#   /move         <sensor_id> <energy>
#   /flick        <sensor_id> <strength>
#   /displacement <sensor_id> <x_m> <y_m> <z_m>
#   /motion_xz    <sensor_id> <x_accel_m_s2> <z_accel_m_s2>
#   /didgeridoo_motion_xz <sensor_id> <x_accel_m_s2> <z_accel_m_s2> (ID 3 copy)
#   /handpan_motion_xz <sensor_id> <x_accel_m_s2> <z_accel_m_s2> (ID 4 copy)
#   /drum_n_didge_motion_xz <sensor_id> <x_accel_m_s2> <z_accel_m_s2> (IDs 3/4 copy)
#
# Each sensor calibrates when it first appears. Hold it still in its starting
# pose for about two seconds; that pose is displacement (0, 0, 0).

import math
import sys
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
MOTION_SEND_INTERVAL = 0.02  # preserve short motion changes at ~50 Hz
SERIAL_BAUD = 230400

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
receiver = serial.Serial(PORT, SERIAL_BAUD, timeout=1)

calibration = {}
state = {}
DISPLAY_IDS = (1, 2, 3, 4)
DISPLAY_INTERVAL = 0.1
dashboard_drawn = False
last_dashboard_update = 0.0


def render_dashboard():
    """Refresh one fixed row per sensor instead of scrolling meter updates."""
    global dashboard_drawn, last_dashboard_update
    now = time.monotonic()
    if now - last_dashboard_update < DISPLAY_INTERVAL:
        return
    last_dashboard_update = now

    rows = []
    for client_id in DISPLAY_IDS:
        client = state.get(client_id)
        if client is None:
            rows.append(f"[{client_id}] waiting for sensor | flicks 0")
        elif not client["calibrated"]:
            rows.append(
                f"[{client_id}] calibrating {len(calibration[client_id]):3d}/{CALIBRATION_SAMPLES} "
                f"| flicks {client['flicks']}"
            )
        else:
            energy = client["energy"]
            bar = "#" * int(energy * 18)
            pos = client["position"]
            rows.append(
                f"[{client_id}] {energy:4.2f} |{bar:<18}| "
                f"d=({pos[0]:+.2f},{pos[1]:+.2f},{pos[2]:+.2f})m "
                f"flicks {client['flicks']}"
            )

    if sys.stdout.isatty():
        if not dashboard_drawn:
            print("\nSensor status (refreshes in place):")
            sys.stdout.write("\n".join(rows) + "\n")
            dashboard_drawn = True
        else:
            sys.stdout.write(f"\x1b[{len(rows)}A")
            for row in rows:
                sys.stdout.write("\r\x1b[2K" + row + "\n")
        sys.stdout.flush()
    else:
        # Keep redirected/non-interactive output to a single refreshed line.
        sys.stdout.write("\r" + " | ".join(rows))
        sys.stdout.flush()

print("Four-sensor bridge running (IDs 1-4). Move! Ctrl+C to stop.\n")
print(f"Reading serial from {PORT} at {SERIAL_BAUD}; close Arduino Serial Monitor first.")
render_dashboard()

while True:
    line = receiver.readline().decode(errors="ignore").strip()
    parts = line.split(",")
    if len(parts) != 8:
        continue
    try:
        sensor = int(parts[0])
        timestamp_ms = int(parts[1])
        ax, ay, az, gx, gy, gz = [float(value) for value in parts[2:]]
    except ValueError:
        continue
    if sensor not in (1, 2, 3, 4):
        continue

    now = time.monotonic()
    if sensor not in state:
        state[sensor] = {
            "energy": 0.0, "ready_for_flick": True, "last_send": 0.0,
            "last_motion_send": 0.0,
            "flicks": 0, "gravity": 0.0, "gyro_offset": [0.0] * 3,
            "gravity_vector": [0.0] * 3, "position": [0.0] * 3,
            "calibrated": False,
            "velocity": [0.0] * 3, "last_time": None,
            "last_print_energy": 0.0,
        }
        calibration[sensor] = []

    readings = calibration[sensor]
    if len(readings) < CALIBRATION_SAMPLES:
        readings.append((ax, ay, az, gx, gy, gz))
        if len(readings) == CALIBRATION_SAMPLES:
            s = state[sensor]
            s["gravity"] = sum(math.sqrt(r[0]**2 + r[1]**2 + r[2]**2) for r in readings) / len(readings)
            s["gravity_vector"] = [sum(r[i] for r in readings) / len(readings) for i in range(3)]
            s["gyro_offset"] = [sum(r[i] for r in readings) / len(readings) for i in (3, 4, 5)]
            s["calibrated"] = True
        render_dashboard()
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

    # Send X/Z together, with the ID, as one synchronized motion frame.
    if now - s["last_motion_send"] >= MOTION_SEND_INTERVAL:
        motion_xz = [sensor, acceleration[X_AXIS], acceleration[Z_AXIS]]
        sonic_pi.send_message("/motion_xz", motion_xz)
        # Keep the instrument streams separate from the shared motion topic.
        if sensor == 3:
            sonic_pi.send_message("/didgeridoo_motion_xz", motion_xz)
        if sensor == 4:
            sonic_pi.send_message("/handpan_motion_xz", motion_xz)
        if sensor in (3, 4):
            # Retain the shared ID 3/4 feed for existing combined-script clients.
            sonic_pi.send_message("/drum_n_didge_motion_xz", motion_xz)
        s["last_motion_send"] = now

    if s["ready_for_flick"] and s["energy"] > FLICK_ON:
        s["flicks"] += 1
        sonic_pi.send_message("/flick", [sensor, s["energy"]])
        s["ready_for_flick"] = False
    elif not s["ready_for_flick"] and s["energy"] < FLICK_OFF:
        s["ready_for_flick"] = True

    render_dashboard()
