# Reads up to four sensor IDs from the receiver ESP32 and sends only the OSC
# paths consumed by all_gloves.rb.
#
# OSC:
#   /flick <sensor_id> <strength> (IDs 1/2 only)
#   /didgeridoo_motion_xz <sensor_id> <x_accel_m_s2> <z_accel_m_s2> (ID 3 only)
#   /handpan_motion_xz <sensor_id> <x_accel_m_s2> <z_accel_m_s2> (ID 4 only)
#
# Each sensor calibrates independently from its first 100 readings. Hold it
# still for about two seconds at 50 Hz while gravity and gyro offsets are read.

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
MOTION_SEND_INTERVAL = 0.02  # match the sender's 50 Hz rate
SERIAL_BAUD = 230400

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
            rows.append(
                f"[{client_id}] {energy:4.2f} |{bar:<18}| "
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
        _timestamp_ms = int(parts[1])
        ax, ay, az, gx, gy, gz = [float(value) for value in parts[2:]]
    except ValueError:
        continue
    if sensor not in (1, 2, 3, 4):
        continue

    now = time.monotonic()
    if sensor not in state:
        state[sensor] = {
            "energy": 0.0, "ready_for_flick": True, "last_motion_send": 0.0,
            "flicks": 0, "gravity": 0.0, "gyro_offset": [0.0] * 3,
            "gravity_vector": [0.0] * 3, "calibrated": False,
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

    turning = gyro_magnitude / GYRO_FULL
    total_accel = math.sqrt(ax * ax + ay * ay + az * az)
    pushing = abs(total_accel - s["gravity"]) / ACCEL_FULL
    raw = min(turning + pushing, 1.0)
    s["energy"] += SMOOTH * (raw - s["energy"])

    if sensor in (3, 4) and now - s["last_motion_send"] >= MOTION_SEND_INTERVAL:
        motion_xz = [sensor, acceleration[0], acceleration[2]]
        if sensor == 3:
            sonic_pi.send_message("/didgeridoo_motion_xz", motion_xz)
        else:
            sonic_pi.send_message("/handpan_motion_xz", motion_xz)
        s["last_motion_send"] = now

    if sensor in (1, 2):
        if s["ready_for_flick"] and s["energy"] > FLICK_ON:
            s["flicks"] += 1
            sonic_pi.send_message("/flick", [sensor, s["energy"]])
            s["ready_for_flick"] = False
        elif not s["ready_for_flick"] and s["energy"] < FLICK_OFF:
            s["ready_for_flick"] = True

    render_dashboard()
