# wearables

Motion-driven music instruments for the course "Designing and Crafting
Wearables": sensor gloves (and a spinning staff) that make music through
Sonic Pi. Each glove has an ESP32 and a MPU-6050 motion sensor; the movement
of your hands becomes sound.

```
glove (ESP32-C3 + MPU6050) --ESP-NOW--> receiver ESP32 (USB) --serial--> Python bridge --OSC--> Sonic Pi
```

## The four gloves

| Sensor ID | Role | Sonic Pi script |
| --------- | ---------------------- | ------------------------------ |
| 1 | synth, left hand | `sonicpi/all_gloves.rb` |
| 2 | synth, right hand | `sonicpi/all_gloves.rb` |
| 3 | circular-motion didgeridoo | `sonicpi/all_gloves.rb` |
| 4 | hangpan drum | `sonicpi/all_gloves.rb` |

**Any combination of gloves works.** Every Sonic Pi script only listens to
its assigned glove IDs, so the synth pair and the ID 3/4 instruments can
play together without sharing their motion channels.

## Preliminary steps

### 1. Install Sonic Pi

1. Download the Sonic Pi AppImage from the official Sonic Pi website.
2. Make it executable and run it:
   ```bash
   chmod +x Sonic-Pi*.AppImage
   ./Sonic-Pi*.AppImage
   ```
3. In Sonic Pi, open *Preferences -> IO* (called *Network* in newer
   versions) and enable **"Allow incoming OSC"** — without this the gloves
   are silent.

### 2. Set up the Python environment

The bridge needs Python 3 with `pyserial` and `python-osc`:

```bash
python3 -m venv .gyroenv
.gyroenv/bin/pip install pyserial python-osc
```

The repo already contains a ready `.gyroenv` with both installed, so you
can usually skip this step.

### 3. Flash the boards (Arduino IDE with the ESP32 core)

**Receiver** (any regular ESP32, e.g. DOIT ESP32 DEVKIT V1):
open `gloves/reciever/reciever.ino`, flash, and leave it connected over
USB. It just forwards radio messages to the laptop.

**Each glove** (ESP32-C3 SuperMini + MPU-6050):
open `stick_sender_smolchange.ino`, set the ID at the top —

```cpp
const int SENSOR_ID = 3;   // 1 + 2 = synth gloves, 3 = bass glove, 4 = drums
```

— and flash. Board settings for the C3: board **"ESP32C3 Dev Module"** and
*Tools -> USB CDC On Boot -> Enabled*, or serial never connects. I2C pins
are SDA 8 / SCL 9. Don't remove the `WiFi.setTxPower(...)` line — it fixes
a known radio problem on these boards.

### 4. Start everything

1. Find the receiver's port: `ls /dev/ | grep tty` (usually
   `/dev/ttyUSB0`). **Close the Arduino Serial Monitor** — only one
   program can open the port.
2. Start the bridge:
   ```bash
   .gyroenv/bin/python sonicpi/4_hand_bridge_calibration.py
   ```
3. In Sonic Pi, paste `all_gloves.rb` into one **empty** buffer and press
   **Run**. Stop older/standalone scripts that listen to these motion
   streams to avoid competing OSC readers. Press **Stop** before running a
   script again. For the split setup, use `two_hand_music.rb` together with
   `drum_n_didge.rb` instead, not at the same time as `all_gloves.rb`.

The bridge prints one meter per connected glove:

```
[3] 0.12 z+0.31 x-0.05 |##        | flicks 4
```

## Calibrating the gloves

Each sensor calibrates independently from its first 100 valid readings.
Hold it still until its dashboard row finishes calibrating (about 10 seconds
for a 10 Hz sender, or two seconds at 50 Hz):

1. Hold each glove still when its dashboard row shows `calibrating`.
2. The bridge uses those readings to estimate gravity and gyro offsets and
   set the initial displacement to zero.

Tips:

- To calibrate again, restart the bridge and hold the desired starting pose
  still during calibration.
- If up/down feels inverted (raising the hand reads a negative z), flip
  `Z_SIGN = -1` in the bridge; same for sideways with `X_SIGN`. These signs
  affect normalized `/z` and `/x` controls, not the didgeridoo/hangpan
  acceleration mappings.

### What each glove does after calibration

- **Synth gloves (1, 2):** movement energy opens a sustained note and a
  whoosh on your side of the stereo field; flicks play high chord notes.
- **Didgeridoo (3):** circular X/Z acceleration controls the continuous
  didgeridoo voice.
- **Hangpan (4):** a downstroke followed by an upstroke triggers the next
  hangpan note.

The bridge retains the existing `/move <id> <energy>` and
`/flick <id> <strength>` messages used by `two_hand_music.rb`. It also keeps
the ID-tagged `/motion_xz` stream and emits separate ID-tagged
`/didgeridoo_motion_xz` (ID 3) and `/handpan_motion_xz` (ID 4) streams for
the combined instrument script.

## Troubleshooting

- **No sound at all:** enable *Allow incoming OSC* in Sonic Pi, check the
  bridge is running and printing meters.
- **Bridge can't open the port:** close the Serial Monitor, check
  `ls /dev/ | grep tty`.
- **C3 glove won't flash/connect over USB:** *USB CDC On Boot -> Enabled*.
- **Gloves connect but feel wrong-sided/upside-down:** flip `Z_SIGN` /
  `X_SIGN` in the bridge (see above).
