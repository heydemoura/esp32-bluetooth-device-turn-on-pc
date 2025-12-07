#include <BLEDevice.h>
#include <BLEScan.h>
#include <LittleFS.h>

#define POWER_PIN 5
#define PC_STATUS_PIN 4  // Reads PC power state
#define POWER_PULSE_DURATION 500
#define MAX_CONTROLLERS 10
#define CONTROLLERS_FILE "/controllers.txt"

String targetControllers[MAX_CONTROLLERS];
int numControllers = 0;

const unsigned long INACTIVITY_TIMEOUT = 2 * 60 * 1000;  // 2 minutes
const unsigned long WAKE_COOLDOWN = 30 * 1000;
const unsigned long STATUS_CHECK_INTERVAL = 5000;  // Check PC status every 5 seconds

unsigned long lastControllerSeen = 0;
bool controllerCurrentlyActive = false;
bool sleepSignalSent = false;
unsigned long lastWakeTime = 0;
unsigned long lastStatusCheck = 0;
bool lastPCState = false;
bool scanMode = false;  // BLE scan debug mode

BLEScan* pBLEScan;

bool isPCOn() {
  // Read voltage divider - adjust threshold based on your setup
  int reading = analogRead(PC_STATUS_PIN);
  // Serial.printf("Reading from PC_STATUS_PIN: %d\n", reading);
  return reading > 1000;  // Tune this value
}

void sendPowerSignal(String reason) {
  Serial.print(">>> Sending power signal: ");
  Serial.println(reason);
  
  pinMode(POWER_PIN, OUTPUT);
  digitalWrite(POWER_PIN, LOW);
  delay(POWER_PULSE_DURATION);
  pinMode(POWER_PIN, INPUT);
  
  Serial.println("Power signal sent");
}

void wakePC() {
  // Check if PC is already on
  if (isPCOn()) {
    Serial.println("PC already on, skipping wake signal");
    return;
  }
  
  unsigned long now = millis();
  if (now - lastWakeTime < WAKE_COOLDOWN) {
    Serial.println("Wake cooldown active, ignoring...");
    return;
  }
  
  sendPowerSignal("WAKE PC");
  lastWakeTime = now;
  sleepSignalSent = false;
}

void sleepPC() {
  // Check if PC is already off/sleeping
  if (!isPCOn()) {
    Serial.println("PC already off/sleeping, skipping sleep signal");
    sleepSignalSent = true;  // Mark as done
    return;
  }
  
  if (sleepSignalSent) {
    return;
  }
  
  sendPowerSignal("SLEEP PC");
  sleepSignalSent = true;
}

void loadControllersFromFile() {
  Serial.println("\n--- Loading controller list ---");

  if (!LittleFS.begin(true)) {
    Serial.println("Failed to mount LittleFS, using defaults");
    targetControllers[0] = "b2:3c:44:d1:5a:8e";
    numControllers = 1;
    return;
  }

  if (!LittleFS.exists(CONTROLLERS_FILE)) {
    Serial.println("controllers.txt not found, creating default file...");
    File file = LittleFS.open(CONTROLLERS_FILE, "w");
    if (file) {
      file.println("# Add your controller MAC addresses here (one per line)");
      file.println("# Lines starting with # are comments");
      file.println("# Format: xx:xx:xx:xx:xx:xx (lowercase)");
      file.println("#");
      file.println("b2:3c:44:d1:5a:8e");
      file.close();
      Serial.println("Default controllers.txt created");
    }
  }

  File file = LittleFS.open(CONTROLLERS_FILE, "r");
  if (!file) {
    Serial.println("Failed to open controllers.txt");
    targetControllers[0] = "b2:3c:44:d1:5a:8e";
    numControllers = 1;
    return;
  }

  numControllers = 0;
  while (file.available() && numControllers < MAX_CONTROLLERS) {
    String line = file.readStringUntil('\n');
    line.trim();

    // Skip empty lines and comments
    if (line.length() == 0 || line.startsWith("#")) {
      continue;
    }

    // Validate MAC address format (basic check)
    line.toLowerCase();
    if (line.length() == 17 && line.charAt(2) == ':' && line.charAt(5) == ':') {
      targetControllers[numControllers] = line;
      Serial.print("  [");
      Serial.print(numControllers + 1);
      Serial.print("] ");
      Serial.println(line);
      numControllers++;
    } else {
      Serial.print("  Invalid MAC format, skipping: ");
      Serial.println(line);
    }
  }

  file.close();

  if (numControllers == 0) {
    Serial.println("No valid controllers found, using default");
    targetControllers[0] = "b2:3c:44:d1:5a:8e";
    numControllers = 1;
  }

  Serial.print("Loaded ");
  Serial.print(numControllers);
  Serial.println(" controller(s)\n");
}

bool validateMAC(String mac) {
  // Length must be exactly 17 characters (xx:xx:xx:xx:xx:xx)
  if (mac.length() != 17) {
    return false;
  }

  // Check colons at positions 2, 5, 8, 11, 14
  if (mac.charAt(2) != ':' || mac.charAt(5) != ':' ||
      mac.charAt(8) != ':' || mac.charAt(11) != ':' ||
      mac.charAt(14) != ':') {
    return false;
  }

  // Check hex characters at all other positions
  for (int i = 0; i < 17; i++) {
    if (i % 3 == 2) continue; // Skip colon positions
    char c = mac.charAt(i);
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }

  return true;
}

void saveControllersToFile() {
  if (!LittleFS.begin(true)) {
    Serial.println("Failed to mount LittleFS, cannot save");
    return;
  }

  File file = LittleFS.open(CONTROLLERS_FILE, "w");
  if (!file) {
    Serial.println("Failed to open controllers.txt for writing");
    return;
  }

  // Write header comments
  file.println("# Controller MAC Addresses");
  file.println("# Add your Bluetooth controller MAC addresses here (one per line)");
  file.println("# Lines starting with # are comments and will be ignored");
  file.println("# Format: xx:xx:xx:xx:xx:xx (lowercase letters)");
  file.println("#");
  file.println("# To find your controller's MAC address:");
  file.println("# - Windows: Settings → Bluetooth & devices → Click \"...\" → Properties");
  file.println("# - macOS: Hold Option, click Bluetooth icon in menu bar");
  file.println("# - Linux: Run \"bluetoothctl\" and type \"devices\"");
  file.println("");
  file.println("# Add your controllers below:");

  // Write all controllers
  for (int i = 0; i < numControllers; i++) {
    file.println(targetControllers[i]);
  }

  file.close();
  Serial.println(">>> Controllers saved to file");
}

void showHelp() {
  Serial.println("\n=== Controller Management Commands ===");
  Serial.println("Available commands:");
  Serial.println("  list                      - Show all configured controllers");
  Serial.println("  add <mac>                 - Add controller (e.g., add aa:bb:cc:dd:ee:ff)");
  Serial.println("  remove <mac>              - Remove controller (e.g., remove aa:bb:cc:dd:ee:ff)");
  Serial.println("  reload                    - Reload controllers from file");
  Serial.println("  scan <on|off>             - Enable/disable BLE scan debug output");
  Serial.println("  help                      - Show this help message");
  Serial.println("");
}

void listControllers() {
  Serial.println("\n=== Configured Controllers ===");
  if (numControllers == 0) {
    Serial.println("  No controllers configured");
  } else {
    for (int i = 0; i < numControllers; i++) {
      Serial.print("  [");
      Serial.print(i + 1);
      Serial.print("] ");
      Serial.println(targetControllers[i]);
    }
  }
  Serial.print("Total: ");
  Serial.print(numControllers);
  Serial.print("/");
  Serial.println(MAX_CONTROLLERS);
  Serial.println("");
}

void addController(String mac) {
  mac.toLowerCase();
  mac.trim();

  // Validate MAC format
  if (!validateMAC(mac)) {
    Serial.print("Invalid MAC address format: ");
    Serial.println(mac);
    Serial.println("Expected format: xx:xx:xx:xx:xx:xx (lowercase)");
    return;
  }

  // Check if already exists
  for (int i = 0; i < numControllers; i++) {
    if (targetControllers[i] == mac) {
      Serial.print("Controller already exists: ");
      Serial.println(mac);
      return;
    }
  }

  // Check if list is full
  if (numControllers >= MAX_CONTROLLERS) {
    Serial.print("Controller list full (");
    Serial.print(MAX_CONTROLLERS);
    Serial.println(" max)");
    return;
  }

  // Add controller
  targetControllers[numControllers] = mac;
  numControllers++;

  Serial.print(">>> Controller added: ");
  Serial.println(mac);

  // Save to file
  saveControllersToFile();
}

void removeController(String mac) {
  mac.toLowerCase();
  mac.trim();

  // Find controller in list
  int foundIndex = -1;
  for (int i = 0; i < numControllers; i++) {
    if (targetControllers[i] == mac) {
      foundIndex = i;
      break;
    }
  }

  if (foundIndex == -1) {
    Serial.print("Controller not found: ");
    Serial.println(mac);
    return;
  }

  // Shift remaining elements left
  for (int i = foundIndex; i < numControllers - 1; i++) {
    targetControllers[i] = targetControllers[i + 1];
  }

  // Clear last element and decrement count
  targetControllers[numControllers - 1] = "";
  numControllers--;

  Serial.print(">>> Controller removed: ");
  Serial.println(mac);

  // Save to file
  saveControllersToFile();
}

void reloadControllers() {
  Serial.println(">>> Reloading controllers from file...");
  loadControllersFromFile();
}

void handleSerialCommand() {
  if (!Serial.available()) {
    return;
  }

  // Read full line (non-blocking if line is ready)
  String command = Serial.readStringUntil('\n');
  command.trim();

  if (command.length() == 0) {
    return;
  }

  // Echo command for clarity
  Serial.print("> ");
  Serial.println(command);

  // Parse command and argument
  int spaceIndex = command.indexOf(' ');
  String cmd = "";
  String arg = "";

  if (spaceIndex == -1) {
    cmd = command;
  } else {
    cmd = command.substring(0, spaceIndex);
    arg = command.substring(spaceIndex + 1);
    arg.trim();
  }

  cmd.toLowerCase();

  // Execute command
  if (cmd == "help") {
    showHelp();
  } else if (cmd == "list") {
    listControllers();
  } else if (cmd == "add") {
    if (arg.length() == 0) {
      Serial.println("Usage: add <mac_address>");
    } else {
      addController(arg);
    }
  } else if (cmd == "remove") {
    if (arg.length() == 0) {
      Serial.println("Usage: remove <mac_address>");
    } else {
      removeController(arg);
    }
  } else if (cmd == "reload") {
    reloadControllers();
  } else if (cmd == "scan") {
    if (arg.length() == 0) {
      Serial.println("Usage: scan <on|off>");
    } else {
      arg.toLowerCase();
      if (arg == "on") {
        scanMode = true;
        Serial.println(">>> BLE scan debug mode: ON");
      } else if (arg == "off") {
        scanMode = false;
        Serial.println(">>> BLE scan debug mode: OFF");
      } else {
        Serial.println("Invalid parameter. Use: scan on or scan off");
      }
    }
  } else {
    Serial.print("Unknown command: ");
    Serial.println(cmd);
    Serial.println("Type 'help' for available commands");
  }
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    String address = advertisedDevice.getAddress().toString();
    address.toLowerCase();

    // Check if this is a target controller
    bool isTarget = false;
    for (int i = 0; i < numControllers; i++) {
      if (address == targetControllers[i]) {
        isTarget = true;
        unsigned long now = millis();

        if (!controllerCurrentlyActive) {
          Serial.print("Controller turned ON: ");
          Serial.println(address);
          wakePC();
          controllerCurrentlyActive = true;
        }

        lastControllerSeen = now;
        break;
      }
    }

    // Show debug output only if scan mode is enabled
    if (scanMode) {
      Serial.print("BLE scan: ");
      Serial.print(address);

      // Add device name if available
      if (advertisedDevice.haveName()) {
        Serial.print(" (");
        Serial.print(advertisedDevice.getName().c_str());
        Serial.print(")");
      } else {
        Serial.print(" (no name)");
      }

      // Add target controller tag
      if (isTarget) {
        Serial.println(" [TARGET CONTROLLER]");
      } else {
        Serial.println();
      }
    }
  }
};

void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(POWER_PIN, INPUT);
  pinMode(PC_STATUS_PIN, INPUT);

  Serial.println("=== PC Wake/Sleep Controller with Status Detection ===");

  // Load controller list from file
  loadControllersFromFile();

  // Check initial PC state
  bool pcOn = isPCOn();
  Serial.print("Initial PC state: ");
  Serial.println(pcOn ? "ON" : "OFF/SLEEP");
  lastPCState = pcOn;

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  lastControllerSeen = millis();

  Serial.println("\nType 'help' for controller management commands\n");
}

void loop() {
  // Check for serial commands (non-blocking)
  handleSerialCommand();

  pBLEScan->start(2, false);
  pBLEScan->clearResults();

  unsigned long now = millis();
  unsigned long timeSinceLastSeen = now - lastControllerSeen;
  
  // Periodic PC status check
  if (now - lastStatusCheck > STATUS_CHECK_INTERVAL) {
    bool pcOn = isPCOn();
    
    // Detect state changes
    if (pcOn != lastPCState) {
      Serial.print("PC state changed: ");
      Serial.println(pcOn ? "ON" : "OFF/SLEEP");
      
      // If PC turned off externally, reset our flags
      if (!pcOn) {
        sleepSignalSent = false;
      }
    }
    
    lastPCState = pcOn;
    lastStatusCheck = now;
  }
  
  // Check inactivity timeout
  if (controllerCurrentlyActive && timeSinceLastSeen > INACTIVITY_TIMEOUT) {
    Serial.println("Controller inactive for 2+ minutes");
    sleepPC();
    controllerCurrentlyActive = false;
  }
  
  // Debug output
  static unsigned long lastDebugPrint = 0;
  if (now - lastDebugPrint > 30000) {
    Serial.print("Status - PC: ");
    Serial.print(lastPCState ? "ON" : "OFF");
    Serial.print(", Controller: ");
    if (controllerCurrentlyActive) {
      Serial.print("Active (last seen ");
      Serial.print(timeSinceLastSeen / 1000);
      Serial.println("s ago)");
    } else {
      Serial.println("Inactive");
    }
    lastDebugPrint = now;
  }
  
  delay(500);
}
