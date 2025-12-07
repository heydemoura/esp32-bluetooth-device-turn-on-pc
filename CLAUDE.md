# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-C3 Arduino project that automatically wakes a PC from sleep when a Bluetooth game controller powers on, and puts it to sleep after 2 minutes of inactivity. 

## Hardware Architecture

**Key Components:**
- ESP32-C3 microcontroller (BLE only - 160KB RAM, 1.3MB flash limit)
- NPN transistor (2N2222) or relay module for power button control
- Voltage divider (10kΩ + 4.7kΩ) for PC state detection

**GPIO Pin Assignments:**
- GPIO 5 (`POWER_PIN`): Controls power button via transistor/relay
- GPIO 4 (`PC_STATUS_PIN`): Reads PC power state via voltage divider (analog input)

**Critical Constraints:**
- ESP32-C3 only supports BLE, not Bluetooth Classic (affects controller compatibility)
- Flash memory limit: 1,310,720 bytes (1.25MB) - code must fit within this
- GPIO analog reads are 0-3.3V max (voltage divider reduces 5V to ~1.6V)
- Power button control uses momentary pulse (500ms default)

## Code Architecture

**Single-file Arduino sketch:** `bluetooth_controller_pc_remote/bluetooth_controller_pc_remote.ino`

**Major Components:**
1. **BLE Controller Detection** - Continuous 2s scan intervals, MAC address matching
2. **PC Power Control** - Transistor-based power button pulsing with safety checks
3. **PC State Monitoring** - Voltage divider reading every 5s
4. **LittleFS Storage** - Persistent controller list and WiFi credentials
5. **Serial Command Interface** - list, add, remove, reload, scan on/off, help

**Core State Machine:**
1. BLE scan runs continuously (2s intervals)
2. When target controller MAC detected → wake PC if sleeping
3. Track last-seen timestamp for controller
4. After 2min inactivity → send sleep signal
5. Periodic PC state monitoring (5s interval) to detect external power changes

**Key State Variables:**
- `controllerCurrentlyActive`: Whether controller was recently detected
- `lastControllerSeen`: Timestamp of last BLE detection
- `sleepSignalSent`: Prevents repeated sleep signals
- `lastPCState`: Tracks PC on/off transitions
- `apMode`: WiFi access point mode active (true) or station mode (false)

**Safety Mechanisms:**
- Wake cooldown (30s) prevents signal spamming
- PC state detection before sending signals (won't wake if already on)
- Transistor/relay isolation protects ESP32 from motherboard signals

## Development Commands

**Initial Setup (First Time):**
1. Open `bluetooth_controller_pc_remote/bluetooth_controller_pc_remote.ino` in Arduino IDE
2. Select board: Tools → Board → ESP32C3 Dev Module
3. Set USB CDC On Boot: Enabled
4. Set Partition Scheme: Default 4MB with spiffs (for LittleFS)
5. Select correct COM port: Tools → Port
6. Install library: Tools → Manage Libraries → Search "ArduinoJson" → Install version 6.x

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

**Serial Commands Available at Runtime:**
- `list` - Show all configured controllers
- `add <mac>` - Add controller (e.g., add aa:bb:cc:dd:ee:ff)
- `remove <mac>` - Remove controller
- `reload` - Reload controllers from file
- `scan on/off` - Enable/disable BLE scan debug output
- `help` - Show command list

## Web Server Architecture

> Not yet implemented, but planned for future versions

**WiFi Setup Flow:**
1. Boot → Try load credentials from `/wifi.json` (LittleFS)
2. If found → Connect to WiFi (station mode)
3. If not found or failed → Start AP mode (SSID: "ESP32-Setup", password: "12345678")
4. AP mode → Captive portal redirects to web interface
5. User configures WiFi → Saves to `/wifi.json` → ESP32 restarts

**REST API Endpoints:**
- `GET /` - HTML dashboard (or setup page in AP mode)
- `GET /api/status` - System status (PC state, controllers, WiFi info)
- `GET /api/controllers` - List controllers (JSON array)
- `POST /api/controllers` - Add controller: `{"mac":"aa:bb:cc:dd:ee:ff"}`
- `DELETE /api/controllers/{mac}` - Remove controller
- `POST /api/reload` - Reload controllers from file
- `GET /api/scan` - BLE scan (5s, returns devices with names)
- `POST /api/wifi/configure` - Set WiFi credentials (AP mode only)

**Memory Optimization:**
- HTML content stored in PROGMEM (flash) to save RAM
- Heavily minified HTML/CSS/JS (~1KB dashboard, ~0.6KB setup)
- Aggressive string optimization in serial messages
- Non-blocking web server calls (`server.handleClient()`)

## Configuration Requirements

**Before deployment, must configure:**

1. **Controller MAC addresses** - Stored in `data/controllers.txt`:
   - Format: one MAC per line, lowercase with colons (xx:xx:xx:xx:xx:xx)
   - Lines starting with # are comments
   - Supports up to 10 controllers (MAX_CONTROLLERS define)
   - Can also manage via serial commands or web API at runtime

2. **Voltage divider threshold** (line 44):
   ```cpp
   return reading > 1000;  // Calibrate per hardware setup
   ```
   - Read `analogRead(PC_STATUS_PIN)` when PC is ON vs OFF
   - Set threshold midway between the two values
   - Typical: ~2000 when ON, <100 when OFF

3. **Timing parameters** (lines 14-16):
   - `INACTIVITY_TIMEOUT`: How long before auto-sleep (default 2min)
   - `WAKE_COOLDOWN`: Prevents wake signal spam (default 30s)
   - `STATUS_CHECK_INTERVAL`: PC state polling frequency (default 5s)

## Flash Memory Constraints

**CRITICAL: ESP32-C3 has 1,310,720 byte flash limit**

If sketch exceeds flash size during compilation:
1. **Reduce HTML content** - Already heavily minified, but can remove features
2. **Shorten serial messages** - Use concise error/status messages
3. **Remove debug output** - Comment out non-essential Serial.println()
4. **Optimize strings** - Avoid duplicate string literals
5. **Reduce functionality** - Consider removing serial commands or web server

Current sketch uses ~1.31MB (100% of available space) - no room for expansion without optimizations.

## Common Modifications

**Add multiple controllers** - Edit `data/controllers.txt` or use web/serial interface:
```
# My controllers
a4:5e:60:c8:2b:1f
b2:3c:44:d1:5a:8e
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
Serial.printf("Analog reading: %d\n", analogRead(PC_STATUS_PIN));
```

## Debugging Serial Output

**Normal operation shows:**
```
ESP32 PC Controller
Loaded 1 controllers
PC: ON

Type 'help' for commands
WiFi: YourNetwork
.........
IP: 192.168.1.100
Web server started
```

**Common issues visible in serial:**
- "PC already on, skipping wake signal" - State detection working correctly
- "Wake cooldown active" - Too many wake attempts (increase cooldown)
- "Inactive timeout" - About to send sleep signal
- "PC state changed: OFF/SLEEP" - External power change detected
- "WiFi disconnected, attempting reconnect..." - Auto-reconnect in progress

## Hardware Troubleshooting Context

**If modifying state detection logic:**
- Voltage divider output should be ~1.6V when 5V present (0V when off)
- Analog read range: 0-4095 (12-bit ADC)
- Typical readings: ~2000 when ON, <100 when OFF
- 5V source must turn off with PC (not USB standby power)

**If modifying power control:**
- Power signal is active-low (transistor pulls PWR_SW- to ground)
- Most motherboards need 100-500ms pulse duration
- Never connect GPIO directly to motherboard (use transistor/relay)

## BLE Compatibility Notes

ESP32-C3 limitation: Only BLE-advertising controllers work.

**Compatible Controllers:**
- 8BitDo controllers: ✅ BLE
- Xbox Wireless: ✅ BLE
- Nintendo Switch Pro: ✅ BLE

**Incompatible:**
- PlayStation DualSense: ❌ Bluetooth Classic discovery (requires ESP32 original/S3)

**Important:** Some controllers advertise with different MAC addresses when paired vs unpaired. Use the web API `/api/scan` or serial command `scan on` to discover the actual advertising MAC address.

## File Structure

```
bluetooth_controller_pc_remote/
  bluetooth_controller_pc_remote.ino  # Main sketch (single file, ~900 lines)
data/
  controllers.txt                      # Controller MAC list (uploaded to LittleFS)
  README.md                           # Instructions for LittleFS upload
controllers.txt.example                # Template for controller list
README.md                             # Hardware setup and usage guide
CLAUDE.md                             # This file
```

## Web Development Workflow

When modifying the web interface:
1. Edit HTML content in `HTML_DASHBOARD` or `HTML_SETUP` constants (lines 884-918)
2. Keep HTML heavily minified to fit flash constraints
3. Use `R"rawliteral(...)rawliteral"` for raw string literals
4. Avoid template literals (backticks) - use string concatenation instead
5. Test in AP mode first (easier to access at 192.168.4.1)
6. Verify total sketch size stays under 1,310,720 bytes
