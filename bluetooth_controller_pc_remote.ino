#include <BLEDevice.h>
#include <BLEScan.h>

#define POWER_PIN 5
#define PC_STATUS_PIN 4  // Reads PC power state
#define POWER_PULSE_DURATION 500

String targetControllers[] = {
  "b2:3c:44:d1:5a:8e"  // Your controller MAC
};
const int numControllers = 1;

const unsigned long INACTIVITY_TIMEOUT = 10 * 60 * 1000;  // 10 minutes
const unsigned long WAKE_COOLDOWN = 30 * 1000;
const unsigned long STATUS_CHECK_INTERVAL = 5000;  // Check PC status every 5 seconds

unsigned long lastControllerSeen = 0;
bool controllerCurrentlyActive = false;
bool sleepSignalSent = false;
unsigned long lastWakeTime = 0;
unsigned long lastStatusCheck = 0;
bool lastPCState = false;

BLEScan* pBLEScan;

bool isPCOn() {
  // Read voltage divider - adjust threshold based on your setup
  int reading = analogRead(PC_STATUS_PIN);
  return reading > 500;  // Tune this value
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

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    String address = advertisedDevice.getAddress().toString();
    address.toLowerCase();
    
    for (int i = 0; i < numControllers; i++) {
      if (address == targetControllers[i].c_str()) {
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
  }
};

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  pinMode(POWER_PIN, INPUT);
  pinMode(PC_STATUS_PIN, INPUT);
  
  Serial.println("=== PC Wake/Sleep Controller with Status Detection ===");
  
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
}

void loop() {
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
    Serial.println("Controller inactive for 10+ minutes");
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