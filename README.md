# PC Wake/Sleep Controller with Bluetooth Detection

Automatically wake your PC from sleep when you turn on your Bluetooth game controller, and put it to sleep after a period of inactivity. Uses an ESP32-C3 microcontroller to detect controller power-on via BLE scanning and monitors PC power state through motherboard voltage sensing.

## Features

- 🎮 **Automatic PC Wake**: Detects when your Bluetooth controller powers on and wakes your PC
- 😴 **Auto Sleep**: Puts PC to sleep after 2 minutes of controller inactivity
- 🔍 **PC State Detection**: Monitors motherboard 5V to determine if PC is on/off
- 🛡️ **Safe Operation**: Only sends power signals when state change is needed
- 📊 **Multiple Controllers**: Supports monitoring multiple Bluetooth controllers
- ⚡ **Low Power**: Runs on motherboard USB standby power

## Compatibility

### Supported Controllers
- ✅ **8BitDo Controllers** (Ultimate Wireless, Pro 2, etc.)
- ✅ **Xbox Wireless Controllers** (Series X|S, One)
- ✅ **Nintendo Switch Pro Controller**
- ⚠️ **PlayStation DualSense** (requires ESP32 with Bluetooth Classic support, not ESP32-C3)

**Note:** The ESP32-C3 only supports **Bluetooth Low Energy (BLE)**. Controllers that use Bluetooth Classic for discovery won't be detected. For full compatibility including DualSense, use ESP32 (original) or ESP32-S3.

### Requirements
- **ESP32-C3** development board
- **Compatible BLE game controller**
- **PC with motherboard** that provides 5V power and USB standby
- **Basic soldering skills** (if using transistor method)

## Hardware Components

### Required Parts

| Component | Quantity | Purpose | Estimated Cost |
|-----------|----------|---------|----------------|
| ESP32-C3 Dev Board | 1 | Microcontroller | $5-8 |
| NPN Transistor (2N2222/2N3904) | 1 | Power button switch | $0.10 |
| 1kΩ Resistor | 1 | Transistor base resistor | $0.01 |
| 10kΩ Resistor | 1 | Voltage divider (R1) | $0.01 |
| 4.7kΩ Resistor | 1 | Voltage divider (R2) | $0.01 |
| Jumper Wires | Several | Connections | $2-3 |

**Alternative:** Replace transistor + resistor with a **5V relay module** ($2-3) for easier assembly.

**Total Cost:** ~$8-12

## Wiring Diagram

### Complete Circuit

```
┌─────────────────────────────────────────────────────────────┐
│                     MOTHERBOARD                              │
│                                                              │
│  PWR_SW Header        5V Source         USB Header          │
│  ┌───┬───┐           (Fan/RGB)          (Standby Power)     │
│  │ + │ - │              │                ┌───┬───┐          │
│  └─┬─┴─┬─┘              │                │ 5V│GND│          │
│    │   │                │                └─┬─┴─┬─┘          │
└────┼───┼────────────────┼──────────────────┼───┼───────────┘
     │   │                │                  │   │
     │   │                │                  │   │
     │   │            ┌───┴───┐              │   │
     │   │            │ 10kΩ  │              │   │
     │   │            └───┬───┘              │   │
     │   │                ├──────────────────┼───┼── GPIO 4 (PC Status)
     │   │            ┌───┴───┐              │   │
     │   │            │ 4.7kΩ │              │   │
     │   │            └───┬───┘              │   │
     │   │                │                  │   │
     │   └────────────────┴──────────────────┴───┼── GND
     │                                            │
     │                                            └── 5V/VIN
     │
     │        ┌──────────────────┐
     └────────┤ Transistor       │
              │  (2N2222)        │
              │                  │
              │  Collector ──────┘
              │  Emitter ────────── GND
              │  Base ──[1kΩ]───── GPIO 5 (Power Control)
              └──────────────────┘

┌─────────────────────────────────────┐
│          ESP32-C3 Board             │
│                                     │
│  GPIO 4  ●  (PC Status Input)       │
│  GPIO 5  ●  (Power Button Control)  │
│  GND     ●  (Common Ground)         │
│  5V/VIN  ●  (USB Standby Power)     │
│                                     │
└─────────────────────────────────────┘
```

### Voltage Divider Detail

The voltage divider reduces 5V to ~1.6V (safe for ESP32):

```
Motherboard 5V ──┬── 10kΩ ──┬── GPIO 4 (reads ~1.6V)
                 │          │
                 │      4.7kΩ
                 │          │
                GND ────────┴── GND
```

### Transistor Switch Detail

```
PWR_SW+ ────── Collector
               │
           [Transistor]
               │
PWR_SW- ────── Emitter ──── GND

GPIO 5 ── [1kΩ] ── Base
```

When GPIO 5 goes HIGH → Transistor closes → PWR+ shorts to PWR- → PC wakes/sleeps

## Step-by-Step Assembly

### 1. Prepare the Voltage Divider

1. Solder 10kΩ and 4.7kΩ resistors in series
2. Connect top of 10kΩ to motherboard 5V source
3. Connect junction between resistors to ESP32 GPIO 4
4. Connect bottom of 4.7kΩ to GND

### 2. Set Up the Power Button Control

**Option A: Using Transistor (Recommended)**

1. Identify transistor pins (flat side facing you: E-B-C from left to right)
2. Connect **Collector** to motherboard **PWR_SW+**
3. Connect **Emitter** to motherboard **PWR_SW-** and ESP32 GND
4. Connect **Base** through 1kΩ resistor to ESP32 **GPIO 5**

**Option B: Using Relay Module (Easier)**

1. Connect relay **VCC** to ESP32 **3.3V**
2. Connect relay **GND** to ESP32 **GND**
3. Connect relay **IN** to ESP32 **GPIO 5**
4. Connect relay **NO** (Normally Open) terminals to motherboard **PWR_SW+** and **PWR_SW-**

### 3. Power the ESP32

1. Locate a USB 2.0 internal header on your motherboard
2. Connect **Pin 1 or 2 (+5V)** to ESP32 **5V/VIN**
3. Connect **Pin 7, 8, or 9 (GND)** to ESP32 **GND**

**Important:** Enable USB standby power in BIOS:
- Go to BIOS → Advanced → USB Configuration
- Enable "USB Power in S4/S5" or similar option
- Disable "ErP" or "EuP 2013" if present

### 4. Find the 5V Source

You need a 5V source that is:
- ✅ **ON when PC is running**
- ✅ **OFF when PC is sleeping/powered off**

**Good 5V sources:**
- **Fan headers** (check with multimeter - may be 5V or 12V)
- **RGB headers** (usually 5V or 12V)
- **Peripheral power connectors**

**Bad 5V sources (avoid these):**
- USB headers (usually standby power, always on)
- Standby power rails

**Testing:** Use a multimeter to verify voltage is present when PC is on and absent when off.

## Software Setup

### 1. Install Arduino IDE

1. Download Arduino IDE from [arduino.cc](https://www.arduino.cc/en/software)
2. Install version 2.x or 1.8.x+

### 2. Add ESP32 Board Support

1. Open Arduino IDE → **File → Preferences**
2. Add to "Additional Board Manager URLs":
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Go to **Tools → Board → Boards Manager**
4. Search for "esp32" and install "**esp32 by Espressif Systems**"

### 3. Configure Board Settings

- **Board**: ESP32C3 Dev Module
- **USB CDC On Boot**: Enabled
- **Upload Speed**: 921600
- **Port**: Select your COM port

### 4. Find Your Controller's MAC Address

**On Windows:**
1. Settings → Bluetooth & devices
2. Find your controller in the list
3. Click "..." → Properties to see MAC address

**On macOS:**
1. Hold Option key, click Bluetooth icon in menu bar
2. Find your controller and note the address

**On Linux:**
```bash
bluetoothctl
devices
```

### 5. Upload the Code

1. Copy the code from `pc-wake-sleep.ino`
2. Replace MAC address in `targetControllers[]` with your controller's MAC
3. Adjust `INACTIVITY_TIMEOUT` if desired (default: 2 minutes)
4. Click **Upload** button
5. Open **Serial Monitor** (115200 baud) to see status

## Configuration

### Controller MAC Addresses

Edit this section in the code:

```cpp
String targetControllers[] = {
  "a4:5e:60:c8:2b:1f",  // Controller 1
  "b2:3c:44:d1:5a:8e",  // Controller 2
  "c8:7f:54:a2:9b:3d"   // Controller 3
};
```

- Use **lowercase** letters
- Format: `xx:xx:xx:xx:xx:xx`
- Add as many as needed

### Timing Settings

```cpp
const unsigned long INACTIVITY_TIMEOUT = 2 * 60 * 1000;  // 2 minutes
const unsigned long WAKE_COOLDOWN = 30 * 1000;            // 30 seconds
const unsigned long STATUS_CHECK_INTERVAL = 5000;         // 5 seconds
```

### Voltage Divider Threshold

If PC state detection is unreliable, adjust this value:

```cpp
bool isPCOn() {
  int reading = analogRead(PC_STATUS_PIN);
  return reading > 500;  // Increase or decrease this value
}
```

**To calibrate:**
1. Open Serial Monitor
2. Add debug line: `Serial.println(analogRead(PC_STATUS_PIN));`
3. Note values when PC is ON vs OFF
4. Set threshold midway between them

## Usage

### Normal Operation

1. **Power on PC manually** (first time)
2. ESP32 begins scanning for controllers
3. **Turn on your controller** → ESP32 detects it → PC wakes (if sleeping)
4. Play games normally
5. **Turn off controller** or let it auto-sleep
6. After 2 minutes of inactivity → ESP32 sends sleep signal to PC

### Serial Monitor Output

```
=== PC Wake/Sleep Controller with Status Detection ===
Initial PC state: ON

--- Scanning ---
Found: b2:3c:44:d1:5a:8e - 8BitDo (RSSI: -65)
Controller turned ON: b2:3c:44:d1:5a:8e
PC already on, skipping wake signal
Found 3 devices

Status - PC: ON, Controller: Active (last seen 12s ago)

[2 minutes later...]
Controller inactive for 2+ minutes
>>> Sending power signal: SLEEP PC
Power signal sent
PC state changed: OFF/SLEEP
```

## Troubleshooting

### Controller Not Detected

**Issue:** ESP32 doesn't see your controller

**Solutions:**
1. Verify controller uses BLE (not Bluetooth Classic)
2. Check MAC address is correct (lowercase, with colons)
3. Ensure controller is in pairing mode when testing
4. Try the debug scanning code to see all devices
5. Move ESP32 closer to controller (within 10 meters)

### PC Not Waking/Sleeping

**Issue:** Power button signal doesn't work

**Solutions:**
1. Verify PWR_SW header connection (check motherboard manual)
2. Test with multimeter: should see continuity when GPIO 5 is HIGH
3. Check transistor orientation (E-B-C pins)
4. Try increasing `POWER_PULSE_DURATION` to 800ms
5. Verify ESP32 has power (check Serial Monitor)

### PC State Detection Wrong

**Issue:** ESP32 thinks PC is on when it's off (or vice versa)

**Solutions:**
1. Check voltage divider connections
2. Measure voltage at GPIO 4 with multimeter (should be 0-1.8V)
3. Adjust threshold in `isPCOn()` function
4. Verify 5V source turns off with PC (not standby power)
5. Add debug prints to see actual analog readings

### ESP32 Not Powering On

**Issue:** No power to ESP32 when PC is off

**Solutions:**
1. Enable USB standby power in BIOS
2. Disable ErP/EuP power saving modes
3. Use external USB power adapter instead
4. Check USB header connections

### Multiple Wake/Sleep Signals

**Issue:** ESP32 keeps triggering power button

**Solutions:**
1. Increase `WAKE_COOLDOWN` value
2. Check PC state detection is working correctly
3. Verify controller isn't rapidly connecting/disconnecting
4. Add more debug output to see what's triggering signals

## Safety Notes

⚠️ **Important Safety Information:**

- ✅ Always use a transistor or relay - **never connect GPIO directly to motherboard**
- ✅ Double-check polarity before connecting power
- ✅ Use a multimeter to verify voltages
- ✅ The voltage divider is necessary - ESP32 GPIO max is 3.3V
- ✅ Disconnect power when making changes to wiring
- ✅ PWR_SW header is low voltage and safe to work with
- ⚠️ Fan and RGB headers may be 12V - always test first!

## Advanced Customization

### Add Controller Names

```cpp
String controllerNames[] = {
  "Living Room Controller",
  "Bedroom Controller",
  "Guest Controller"
};

// In callback, replace serial output with:
Serial.print("✓ ");
Serial.print(controllerNames[i]);
Serial.println(" detected!");
```

### Different Sleep Trigger

Instead of time-based sleep, sleep when controller itself sleeps:

```cpp
// Detect when controller stops advertising (turned off)
// Immediately send sleep signal
if (wasActive && !nowActive) {
  sleepPC();
}
```

### Network-Based PC Detection

Replace voltage sensing with network ping:

```cpp
#include <WiFi.h>
#include <Ping.h>

bool isPCOn() {
  return Ping.ping("192.168.1.100", 1);
}
```

### Multiple Wake Sources

Combine controller detection with other triggers:

```cpp
// Wake on controller OR motion sensor OR button press
if (controllerDetected || motionDetected || buttonPressed) {
  wakePC();
}
```

## FAQ

**Q: Will this work with PlayStation DualSense controllers?**  
A: No, the ESP32-C3 only supports BLE. DualSense uses Bluetooth Classic for discovery. Use ESP32 (original) or ESP32-S3 instead.

**Q: Can I use this with multiple PCs?**  
A: Not directly, but you could use WiFi to communicate which PC to wake, or use multiple ESP32 boards.

**Q: Does this drain my controller battery?**  
A: No, the ESP32 only listens passively. It doesn't connect to or interact with your controller.

**Q: What if my PC doesn't have USB standby power?**  
A: Use an external USB power adapter (5V) or enable USB standby in BIOS.

**Q: Can I make it wake via keyboard/mouse instead?**  
A: Yes, but you'd need to use Wake-on-LAN or USB HID emulation instead.

**Q: Will this work on laptops?**  
A: Technically yes, but accessing internal headers is more difficult. External power button control is easier.

## Contributing

Contributions are welcome! Areas for improvement:

- Support for more controller types
- Better PC state detection methods
- Mobile app for configuration
- Multiple PC support
- Power consumption optimization

## License

This project is open source and available under the MIT License.

## Credits

Created as a solution for automatic PC wake/sleep based on Bluetooth game controller activity.

## Links

- [ESP32-C3 Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/)
- [Arduino ESP32 BLE Library](https://github.com/espressif/arduino-esp32)
- [Motherboard PWR_SW Header Pinouts](https://www.pcworld.com/article/560935/how-to-connect-your-cases-power-switch-reset-button-and-leds-to-your-motherboard.html)

---

**Enjoy automatic PC wake/sleep with your game controllers!** 🎮💤
