# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an ESP32-C3 Arduino project that automatically wakes a PC from sleep when a Bluetooth game controller is turned on, and puts it to sleep after inactivity. The system uses BLE scanning to detect controller power-on events and monitors PC power state through motherboard voltage sensing.

## Hardware Architecture

**Key Components:**
- ESP32-C3 microcontroller (BLE only - no Bluetooth Classic)
- NPN transistor (2N2222) or relay module for power button control
- Voltage divider (10kΩ + 4.7kΩ) for PC state detection

**GPIO Pin Assignments:**
- GPIO 5 (`POWER_PIN`): Controls power button via transistor/relay
- GPIO 4 (`PC_STATUS_PIN`): Reads PC power state via voltage divider (analog input)

**Critical Constraints:**
- ESP32-C3 only supports BLE, not Bluetooth Classic (affects controller compatibility)
- GPIO analog reads are 0-3.3V max (voltage divider reduces 5V to ~1.6V)
- Power button control uses momentary pulse (500ms default)

## Code Architecture

**Single-file Arduino sketch:** `bluetooth_controller_pc_remote.ino`

**Core State Machine:**
1. BLE scan runs continuously (2s intervals)
2. When target controller MAC detected → wake PC if sleeping
3. Track last-seen timestamp for controller
4. After 10min inactivity → send sleep signal
5. Periodic PC state monitoring (5s interval) to detect external power changes

**Key State Variables:**
- `controllerCurrentlyActive`: Whether controller was recently detected
- `lastControllerSeen`: Timestamp of last BLE detection
- `sleepSignalSent`: Prevents repeated sleep signals
- `lastPCState`: Tracks PC on/off transitions

**Safety Mechanisms:**
- Wake cooldown (30s) prevents signal spamming
- PC state detection before sending signals (won't wake if already on)
- Transistor/relay isolation protects ESP32 from motherboard signals

## Development Commands

**Initial Setup (First Time):**
1. Open `bluetooth_controller_pc_remote.ino` in Arduino IDE
2. Select board: Tools → Board → ESP32C3 Dev Module
3. Set USB CDC On Boot: Enabled
4. Set Partition Scheme: Default 4MB with spiffs (for LittleFS)
5. Select correct COM port: Tools → Port

**Upload Filesystem (controllers.txt):**
1. Install ESP32 LittleFS Upload Plugin:
   - Download: https://github.com/lorol/arduino-esp32littlefs-plugin/releases
   - Extract to `<Arduino>/tools/ESP32LittleFS/tool/esp32littlefs.jar`
   - Restart Arduino IDE

2. Edit `data/controllers.txt` with your controller MAC addresses

3. Upload filesystem: Tools → ESP32 Sketch Data Upload
   - This uploads everything in the `data/` folder to ESP32
   - Only needed when `controllers.txt` changes

**Upload Sketch:**
- Click Upload button or use Ctrl/Cmd + U
- Can upload sketch independently of filesystem

**Serial Monitor:**
- Baud rate: 115200
- Open via: Tools → Serial Monitor or Ctrl/Cmd + Shift + M
- Shows BLE scan results, controller detection, and power state changes

**Development Workflow:**
1. Edit `data/controllers.txt` → Upload filesystem (one time)
2. Edit code → Upload sketch (as needed)
3. No need to re-upload filesystem unless controllers change

## Configuration Requirements

**Before deployment, must configure:**

1. **Controller MAC addresses** - Stored in `data/controllers.txt` (bundled with build):
   - Edit `data/controllers.txt` before uploading
   - Format: one MAC per line, lowercase with colons (xx:xx:xx:xx:xx:xx)
   - Lines starting with # are comments
   - Supports up to 10 controllers (MAX_CONTROLLERS define)
   - Find MAC via OS Bluetooth settings or `bluetoothctl devices`
   - Upload to ESP32 using: Tools → ESP32 Sketch Data Upload
   - File is auto-created with defaults if not found on filesystem

2. **Voltage divider threshold** (line 29):
   ```cpp
   return reading > 500;  // Calibrate per hardware setup
   ```
   - Read `analogRead(PC_STATUS_PIN)` when PC is ON vs OFF
   - Set threshold midway between the two values

3. **Timing parameters** (lines 13-15):
   - `INACTIVITY_TIMEOUT`: How long before auto-sleep (default 2min)
   - `WAKE_COOLDOWN`: Prevents wake signal spam (default 30s)
   - `STATUS_CHECK_INTERVAL`: PC state polling frequency (default 5s)

## Common Modifications

**Add multiple controllers** - Edit `/controllers.txt`:
```
# My controllers
a4:5e:60:c8:2b:1f
b2:3c:44:d1:5a:8e
c8:7f:54:a2:9b:3d
```

**Change max controller limit:**
```cpp
#define MAX_CONTROLLERS 20  // Increase from 10
```

**Adjust power button pulse duration** (if PC doesn't respond):
```cpp
#define POWER_PULSE_DURATION 800  // Increase from 500ms
```

**Debug voltage readings** (add to loop for calibration):
```cpp
Serial.print("Analog reading: ");
Serial.println(analogRead(PC_STATUS_PIN));
```

**Change sleep trigger** (immediate vs time-based):
Replace lines 153-157 with immediate sleep on controller disconnect.

## Debugging Serial Output

**Normal operation shows:**
```
=== PC Wake/Sleep Controller with Status Detection ===

--- Loading controller list ---
  [1] b2:3c:44:d1:5a:8e
Loaded 1 controller(s)

Initial PC state: ON
Controller turned ON: b2:3c:44:d1:5a:8e
PC already on, skipping wake signal
Status - PC: ON, Controller: Active (last seen 12s ago)
```

**Common issues visible in serial:**
- "PC already on, skipping wake signal" - State detection working correctly
- "Wake cooldown active" - Too many wake attempts (increase cooldown)
- "Controller inactive for 2+ minutes" - About to send sleep signal
- "PC state changed: OFF/SLEEP" - External power change detected

## Hardware Troubleshooting Context

**If modifying state detection logic, note:**
- Voltage divider output should be ~1.6V when 5V present (0V when off)
- Analog read range: 0-4095 (12-bit ADC)
- Typical readings: ~2000 when ON, <100 when OFF
- 5V source must turn off with PC (not USB standby power)

**If modifying power control:**
- Power signal is active-low (transistor pulls PWR_SW- to ground)
- Most motherboards need 100-500ms pulse duration
- Never connect GPIO directly to motherboard (use transistor/relay)

## BLE Compatibility Notes

ESP32-C3 limitation: Only BLE-advertising controllers work. If adding controller support:
- 8BitDo controllers: ✅ BLE (compatible)
- Xbox Wireless: ✅ BLE (compatible)
- Nintendo Switch Pro: ✅ BLE (compatible)
- PlayStation DualSense: ❌ Bluetooth Classic discovery (incompatible with ESP32-C3)

For Bluetooth Classic support, would need ESP32 (original) or ESP32-S3 variant.
