#include <BLEDevice.h>
#include <BLEScan.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>

#define POWER_PIN 5
#define PC_STATUS_PIN 4  // Reads PC power state
#define POWER_PULSE_DURATION 500
#define MAX_CONTROLLERS 10
#define CONTROLLERS_FILE "/controllers.txt"

// WiFi Configuration
#define WIFI_CONFIG_FILE "/wifi.json"
#define AP_SSID "ESP32-Controller-Setup"
#define AP_PASSWORD ""  // Open AP
#define DNS_PORT 53
#define WEB_SERVER_PORT 80
#define MAX_SCAN_RESULTS 50

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

// WiFi state
bool apMode = false;
String wifiSSID = "";
String wifiPassword = "";
unsigned long lastWiFiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 30000;  // 30 seconds

// Server instances
WebServer server(WEB_SERVER_PORT);
DNSServer dnsServer;

// BLE scan results for /api/scan endpoint
struct BLEDeviceInfo {
  String mac;
  String name;
  bool isTarget;
};
BLEDeviceInfo scanResults[MAX_SCAN_RESULTS];
int scanResultCount = 0;
bool scanInProgress = false;

bool isPCOn() {
  // Read voltage divider - adjust threshold based on your setup
  int reading = analogRead(PC_STATUS_PIN);
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
  if (!LittleFS.begin(true)) {
    targetControllers[0] = "b2:3c:44:d1:5a:8e";
    numControllers = 1;
    return;
  }
  if (!LittleFS.exists(CONTROLLERS_FILE)) {
    File file = LittleFS.open(CONTROLLERS_FILE, "w");
    if (file) {
      file.println("b2:3c:44:d1:5a:8e");
      file.close();
    }
  }
  File file = LittleFS.open(CONTROLLERS_FILE, "r");
  if (!file) {
    targetControllers[0] = "b2:3c:44:d1:5a:8e";
    numControllers = 1;
    return;
  }
  numControllers = 0;
  while (file.available() && numControllers < MAX_CONTROLLERS) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line.startsWith("#")) continue;
    line.toLowerCase();
    if (line.length() == 17 && line.charAt(2) == ':' && line.charAt(5) == ':') {
      targetControllers[numControllers++] = line;
    }
  }
  file.close();
  if (numControllers == 0) {
    targetControllers[0] = "b2:3c:44:d1:5a:8e";
    numControllers = 1;
  }
  Serial.print("Loaded ");
  Serial.print(numControllers);
  Serial.println(" controllers");
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
  if (!LittleFS.begin(true)) return;
  File file = LittleFS.open(CONTROLLERS_FILE, "w");
  if (!file) return;
  for (int i = 0; i < numControllers; i++) {
    file.println(targetControllers[i]);
  }
  file.close();
  Serial.println("Saved");
}

void showHelp() {
  Serial.println("\nCommands: list, add <mac>, remove <mac>, reload, scan <on|off>, help\n");
}

void listControllers() {
  Serial.println("\nControllers:");
  if (numControllers == 0) {
    Serial.println("None");
  } else {
    for (int i = 0; i < numControllers; i++) {
      Serial.print(i + 1);
      Serial.print(". ");
      Serial.println(targetControllers[i]);
    }
  }
}

void addController(String mac) {
  mac.toLowerCase();
  mac.trim();
  if (!validateMAC(mac)) {
    Serial.println("Invalid MAC");
    return;
  }
  for (int i = 0; i < numControllers; i++) {
    if (targetControllers[i] == mac) {
      Serial.println("Already exists");
      return;
    }
  }
  if (numControllers >= MAX_CONTROLLERS) {
    Serial.println("List full");
    return;
  }
  targetControllers[numControllers++] = mac;
  Serial.print("Added: ");
  Serial.println(mac);
  saveControllersToFile();
}

void removeController(String mac) {
  mac.toLowerCase();
  mac.trim();
  int foundIndex = -1;
  for (int i = 0; i < numControllers; i++) {
    if (targetControllers[i] == mac) {
      foundIndex = i;
      break;
    }
  }
  if (foundIndex == -1) {
    Serial.println("Not found");
    return;
  }
  for (int i = foundIndex; i < numControllers - 1; i++) {
    targetControllers[i] = targetControllers[i + 1];
  }
  targetControllers[numControllers - 1] = "";
  numControllers--;
  Serial.print("Removed: ");
  Serial.println(mac);
  saveControllersToFile();
}

void reloadControllers() {
  Serial.println("Reloading...");
  loadControllersFromFile();
}

// ========== WiFi Management Functions ==========

bool loadWiFiCredentials() {
  if (!LittleFS.begin(true)) return false;
  if (!LittleFS.exists(WIFI_CONFIG_FILE)) return false;
  File file = LittleFS.open(WIFI_CONFIG_FILE, "r");
  if (!file) return false;

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) return false;
  wifiSSID = doc["ssid"].as<String>();
  wifiPassword = doc["password"].as<String>();
  if (wifiSSID.length() == 0) return false;
  return true;
}

bool saveWiFiCredentials(String ssid, String password) {
  if (!LittleFS.begin(true)) return false;
  File file = LittleFS.open(WIFI_CONFIG_FILE, "w");
  if (!file) return false;
  StaticJsonDocument<256> doc;
  doc["ssid"] = ssid;
  doc["password"] = password;
  if (serializeJson(doc, file) == 0) {
    file.close();
    return false;
  }
  file.close();
  return true;
}

void startAPMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  IPAddress apIP(192, 168, 4, 1);
  IPAddress netMask(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, apIP, netMask);
  Serial.print("AP: ");
  Serial.println(AP_SSID);
  dnsServer.start(DNS_PORT, "*", apIP);
  apMode = true;
}

bool connectToWiFi() {
  if (wifiSSID.length() == 0) return false;
  Serial.print("WiFi: ");
  Serial.println(wifiSSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    apMode = false;
    return true;
  } else {
    Serial.println("WiFi connection failed");
    return false;
  }
}

void checkWiFiConnection() {
  if (apMode) return;

  unsigned long now = millis();
  if (now - lastWiFiCheck < WIFI_CHECK_INTERVAL) return;
  lastWiFiCheck = now;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, attempting reconnect...");
    connectToWiFi();
  }
}

// ========== JSON Helper Functions ==========

void sendJsonResponse(int statusCode, const String& json) {
  server.send(statusCode, "application/json", json);
}

void sendJsonError(int statusCode, const String& message) {
  StaticJsonDocument<128> doc;
  doc["error"] = message;
  String json;
  serializeJson(doc, json);
  sendJsonResponse(statusCode, json);
}

void sendJsonSuccess(const String& message) {
  StaticJsonDocument<128> doc;
  doc["success"] = true;
  doc["message"] = message;
  String json;
  serializeJson(doc, json);
  sendJsonResponse(200, json);
}

String controllersToJson() {
  DynamicJsonDocument doc(32 + (numControllers * 24));
  JsonArray array = doc.to<JsonArray>();

  for (int i = 0; i < numControllers; i++) {
    array.add(targetControllers[i]);
  }

  String json;
  serializeJson(doc, json);
  return json;
}

String getStatusJson() {
  StaticJsonDocument<512> doc;

  doc["pcState"] = lastPCState ? "ON" : "OFF";
  doc["controllerActive"] = controllerCurrentlyActive;
  doc["numControllers"] = numControllers;
  doc["maxControllers"] = MAX_CONTROLLERS;
  doc["scanMode"] = scanMode;
  doc["wifiMode"] = apMode ? "AP" : "STA";
  doc["ipAddress"] = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();

  if (controllerCurrentlyActive) {
    unsigned long timeSince = (millis() - lastControllerSeen) / 1000;
    doc["lastSeenSeconds"] = timeSince;
  }

  String json;
  serializeJson(doc, json);
  return json;
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

// ========== BLE Scan Result Collector ==========

class ScanResultCollector: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (scanResultCount >= MAX_SCAN_RESULTS) return;

    String address = advertisedDevice.getAddress().toString();
    address.toLowerCase();

    // Check if already in results
    for (int i = 0; i < scanResultCount; i++) {
      if (scanResults[i].mac == address) {
        return;  // Already have this device
      }
    }

    // Check if this is a target controller
    bool isTarget = false;
    for (int i = 0; i < numControllers; i++) {
      if (address == targetControllers[i]) {
        isTarget = true;
        break;
      }
    }

    // Add to results
    scanResults[scanResultCount].mac = address;
    scanResults[scanResultCount].name = advertisedDevice.haveName()
      ? String(advertisedDevice.getName().c_str())
      : "Unknown";
    scanResults[scanResultCount].isTarget = isTarget;
    scanResultCount++;
  }
};

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

// ========== REST API Handlers ==========

void handleApiStatus() {
  sendJsonResponse(200, getStatusJson());
}

void handleApiGetControllers() {
  sendJsonResponse(200, controllersToJson());
}

void handleApiAddController() {
  if (server.method() != HTTP_POST) {
    sendJsonError(405, "Method not allowed");
    return;
  }

  StaticJsonDocument<128> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));

  if (error) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  if (!doc.containsKey("mac")) {
    sendJsonError(400, "Missing 'mac' field");
    return;
  }

  String mac = doc["mac"].as<String>();
  mac.toLowerCase();
  mac.trim();

  if (!validateMAC(mac)) {
    sendJsonError(400, "Invalid MAC address format");
    return;
  }

  for (int i = 0; i < numControllers; i++) {
    if (targetControllers[i] == mac) {
      sendJsonError(409, "Controller already exists");
      return;
    }
  }

  if (numControllers >= MAX_CONTROLLERS) {
    sendJsonError(400, "Controller list full");
    return;
  }

  targetControllers[numControllers] = mac;
  numControllers++;
  saveControllersToFile();

  sendJsonSuccess("Controller added: " + mac);
}

void handleApiDeleteController() {
  if (server.method() != HTTP_DELETE) {
    sendJsonError(405, "Method not allowed");
    return;
  }

  // Extract MAC from URI: /api/controllers/aa:bb:cc:dd:ee:ff
  String uri = server.uri();
  int lastSlash = uri.lastIndexOf('/');
  if (lastSlash == -1 || lastSlash == uri.length() - 1) {
    sendJsonError(400, "Missing MAC address in path");
    return;
  }

  String mac = uri.substring(lastSlash + 1);
  mac.toLowerCase();
  mac.trim();

  int foundIndex = -1;
  for (int i = 0; i < numControllers; i++) {
    if (targetControllers[i] == mac) {
      foundIndex = i;
      break;
    }
  }

  if (foundIndex == -1) {
    sendJsonError(404, "Controller not found");
    return;
  }

  for (int i = foundIndex; i < numControllers - 1; i++) {
    targetControllers[i] = targetControllers[i + 1];
  }

  targetControllers[numControllers - 1] = "";
  numControllers--;
  saveControllersToFile();

  sendJsonSuccess("Controller removed: " + mac);
}

void handleApiReload() {
  if (server.method() != HTTP_POST) {
    sendJsonError(405, "Method not allowed");
    return;
  }

  loadControllersFromFile();
  sendJsonSuccess("Controllers reloaded");
}

void handleApiScan() {
  scanResultCount = 0;

  BLEScan* tempScan = BLEDevice::getScan();
  tempScan->setAdvertisedDeviceCallbacks(new ScanResultCollector());
  tempScan->setActiveScan(true);

  scanInProgress = true;
  BLEScanResults* foundDevices = tempScan->start(5, false);
  scanInProgress = false;

  tempScan->clearResults();
  tempScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());

  DynamicJsonDocument doc(64 + (scanResultCount * 80));
  JsonArray array = doc.to<JsonArray>();

  for (int i = 0; i < scanResultCount; i++) {
    JsonObject obj = array.createNestedObject();
    obj["mac"] = scanResults[i].mac;
    obj["name"] = scanResults[i].name;
    obj["isTarget"] = scanResults[i].isTarget;
  }

  String json;
  serializeJson(doc, json);
  sendJsonResponse(200, json);
}

void handleApiWiFiConfigure() {
  if (!apMode) {
    sendJsonError(403, "WiFi configuration only available in AP mode");
    return;
  }

  if (server.method() != HTTP_POST) {
    sendJsonError(405, "Method not allowed");
    return;
  }

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));

  if (error) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  if (!doc.containsKey("ssid")) {
    sendJsonError(400, "Missing 'ssid' field");
    return;
  }

  String newSSID = doc["ssid"].as<String>();
  String newPassword = doc["password"].as<String>();

  if (newSSID.length() == 0) {
    sendJsonError(400, "SSID cannot be empty");
    return;
  }

  if (!saveWiFiCredentials(newSSID, newPassword)) {
    sendJsonError(500, "Failed to save credentials");
    return;
  }

  sendJsonSuccess("WiFi configured - rebooting to connect...");

  delay(2000);
  ESP.restart();
}

// ========== HTML Content (PROGMEM) ==========

const char HTML_DASHBOARD[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>ESP32</title>
<style>body{font-family:Arial;margin:20px}h2{color:#333}ul{list-style:none;padding:0}
li{background:#fff;padding:10px;margin:5px 0}input{padding:8px;margin:5px}
button{padding:8px 12px;background:#2196F3;color:#fff;border:none;cursor:pointer;margin:5px}
.del{background:#f44}#msg{margin:10px 0}</style></head><body>
<h2>Controllers</h2><ul id="list"></ul><div><input id="mac" placeholder="aa:bb:cc:dd:ee:ff"><button onclick="add()">Add</button>
<button onclick="load()">Refresh</button></div><h2>Scan</h2><button onclick="scan()">Scan BLE</button>
<div id="scan"></div><div id="msg"></div><script>
async function api(e,t,n){const o={method:t||'GET'};n&&(o.headers={'Content-Type':'application/json'},o.body=JSON.stringify(n));
return await(await fetch('/api/'+e,o)).json()}
async function load(){const e=await api('controllers');const t=document.getElementById('list');t.innerHTML='';
e.forEach(e=>{const n=document.createElement('li');n.innerHTML=e+' <button class=del onclick="del(\''+e+'\')">X</button>';t.appendChild(n)})}
async function add(){const e=document.getElementById('mac').value.trim().toLowerCase();const t=document.getElementById('msg');
try{const n=await api('controllers','POST',{mac:e});t.textContent=n.message;document.getElementById('mac').value='';load()}
catch(e){t.textContent='Error: '+e.message}}
async function del(e){if(confirm('Remove '+e+'?')){await api('controllers/'+encodeURIComponent(e),'DELETE');load()}}
async function scan(){const e=document.getElementById('scan');e.innerHTML='Scanning...';const t=await api('scan');e.innerHTML='';
t.forEach(t=>{const n=document.createElement('div');n.textContent=t.mac+' - '+t.name+(t.isTarget?' [CFG]':'');e.appendChild(n)})}
load()</script></body></html>
)rawliteral";

const char HTML_SETUP[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>WiFi Setup</title>
<style>body{font-family:Arial;margin:50px auto;max-width:400px}h1{color:#333}input{width:100%;
padding:10px;margin:10px 0;box-sizing:border-box}button{width:100%;
padding:12px;background:#2196F3;color:#fff;border:none;cursor:pointer}
#msg{margin:15px 0;text-align:center}</style></head><body><h1>WiFi Setup</h1><p>Connect to your network:</p>
<form onsubmit="sub(event)"><input id="ssid" placeholder="Network Name" required><input type="password" id="pass" placeholder="Password">
<button>Connect</button></form><div id="msg"></div><script>async function sub(e){e.preventDefault();const t=document.getElementById('ssid').value;
const n=document.getElementById('pass').value;const s=document.getElementById('msg');try{const e=await fetch('/api/wifi/configure',
{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:t,password:n})});const o=await e.json();
s.style.color='green';s.textContent=o.message||'Configured! Restarting...'}catch(e){s.style.color='red';s.textContent='Error: '+e.message}}
</script></body></html>
)rawliteral";

void handleRoot() {
  if (apMode) {
    server.send(200, "text/html", HTML_SETUP);
  } else {
    server.send(200, "text/html", HTML_DASHBOARD);
  }
}

void handleNotFound() {
  sendJsonError(404, "Endpoint not found");
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/controllers", HTTP_GET, handleApiGetControllers);
  server.on("/api/controllers", HTTP_POST, handleApiAddController);
  server.on("/api/reload", HTTP_POST, handleApiReload);
  server.on("/api/scan", HTTP_GET, handleApiScan);
  server.on("/api/wifi/configure", HTTP_POST, handleApiWiFiConfigure);

  // Custom handler for DELETE /api/controllers/{mac}
  server.onNotFound([]() {
    String uri = server.uri();
    if (server.method() == HTTP_DELETE && uri.startsWith("/api/controllers/")) {
      handleApiDeleteController();
    } else {
      handleNotFound();
    }
  });

  server.begin();
  Serial.println("Web server started");
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  pinMode(POWER_PIN, INPUT);
  pinMode(PC_STATUS_PIN, INPUT);
  Serial.println("\nESP32 PC Controller");
  loadControllersFromFile();
  lastPCState = isPCOn();
  Serial.print("PC: ");
  Serial.println(lastPCState ? "ON" : "OFF");

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  lastControllerSeen = millis();

  // Initialize WiFi
  if (loadWiFiCredentials()) {
    if (!connectToWiFi()) startAPMode();
  } else {
    startAPMode();
  }
  setupWebServer();
  Serial.println("\nType 'help' for commands");
}

void loop() {
  // Handle web server and WiFi
  server.handleClient();           // Handle HTTP requests
  if (apMode) dnsServer.processNextRequest();  // Captive portal
  if (!apMode) checkWiFiConnection();          // Auto-reconnect

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
    Serial.println("Inactive timeout");
    sleepPC();
    controllerCurrentlyActive = false;
  }

  delay(500);
}
