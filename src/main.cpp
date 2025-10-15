// =====================================================================
// Overall Code Description
// =====================================================================
// This code serves as a starting point template for ESP8266-based projects that require WiFi configuration.
// It supports both DHCP and static IP setups, allowing the device to operate in two modes:
// - CONFIG mode: Acts as an Access Point (AP) for configuration via a web interface.
// - RUN mode: Connects to a specified WiFi network and runs the main application logic.
// 
// Key Features:
// - Web server for configuration (network settings, JSON editor, restart, factory reset).
// - EEPROM storage for persistent configuration.
// - Button handling for mode toggling and factory reset.
// - JSON-based configuration for easy extension.
// - Default configuration applied if EEPROM is empty or invalid.
// 
// Hardware Requirements:
// - ESP8266 module (e.g., ESP-01).
// - Button connected to GPIO0 (BUTTON_PIN) for mode control and reset.
// - Optional: NeoPixel or other libraries if extended.
//
// Usage Instructions:
// 1. Flash this code to your ESP8266.
// 2. On first boot or after factory reset, it enters CONFIG mode and starts an AP (SSID like "ESP01_AP_XXXXXX", password "12345678").
// 3. Connect to the AP from a device, access http://192.168.4.1 (default AP IP).
// 4. Use the web interface to set WiFi SSID, password, IP settings, etc.
// 5. Save and restart to RUN mode.
// 6. In RUN mode, add your application logic in the loop() under STATE_RUN.
// 7. Press button >2s to toggle modes; hold >20s for factory reset.
// 
// Customization Tips:
// - Extend the JSON config for your project-specific settings (e.g., add sensor params).
// - Add more web routes or features in configureWebServerRoutes().
// - Do not modify existing code; extend by adding new functions or sections.
// - Monitor Serial output (115200 baud) for debugging.
// 
// Warnings:
// - EEPROM size is set to 2048 bytes; adjust if needed but ensure it fits your config.
// - Web server uses port 80; ensure no conflicts.
// - Security: AP password is hardcoded; change for production.
// - Libraries: Ensure all included libraries are installed in Arduino IDE.

#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <time.h>
#include <Bounce2.h>

// =====================================================================
// Enum Definitions
// =====================================================================
// This enum defines the possible states of the device:
// - STATE_CONFIG: Configuration mode where the device acts as an AP and serves a web interface for setup.
// - STATE_RUN: Normal operation mode where the device connects to WiFi and runs the main loop logic.
// The state is determined during WiFi initialization based on config or connection success.

enum DeviceState {
  STATE_CONFIG,
  STATE_RUN
};

// =====================================================================
// Forward Declarations
// =====================================================================
// These are forward declarations of functions used later in the code.
// They are necessary to avoid compilation errors due to functions being called before their definitions.
// Each function's purpose is detailed in its own comment block below.

void loadConfigFromEEPROM();
void parseConfig(const char* jsonConfig);
void setDeviceHostname();
void startAPMode();
void setAPSSID();
void configureWebServerRoutes();
void performFactoryReset();
void saveConfigToEEPROM(const char* newConfig);
void sendHtmlHeader(const char* title);
void sendHtmlFooter();
void handleFavicon();

// =====================================================================
// Globals
// =====================================================================
// These are global variables used throughout the code.
// - EEPROM_SIZE: Defines the allocated EEPROM space for config storage (2048 bytes; can be adjusted if more space needed).
// - ssid, password: Store the WiFi network credentials loaded from config.
// - ap_ssid: Dynamically generated AP SSID based on chip ID.
// - ap_password: Hardcoded password for the AP (change for security in production).
// - BUTTON_PIN: GPIO pin for the button (GPIO0 on ESP-01; uses internal pull-up).
// - server: Instance of the web server on port 80.
// - button: Bounce2 instance for debounced button input.
// - currentConfig: Buffer to hold the current JSON config from EEPROM.
// - currentState: Current device state (CONFIG or RUN).
// - mode: String indicating boot mode ("RUN" or "CONFIG") from config.
// - defaultConfigJson: Default JSON config applied on first boot or reset.
//   - Includes network settings with defaults like "None" for SSID/password, DHCP enabled.
// - css: PROGMEM-stored CSS for web interface styling (keeps it in flash memory to save RAM).
// - database_icon_png: PROGMEM-stored favicon image data (PNG format, 16x16 pixels).
// - database_icon_png_len: Length of the favicon data array.

const int EEPROM_SIZE = 2048;

char ssid[32]     = "";
char password[32] = "";
char ap_ssid[20];
const char* ap_password = "12345678";

#define BUTTON_PIN 0  // GPIO0

ESP8266WebServer server(80);
Bounce2::Button button = Bounce2::Button();

char currentConfig[EEPROM_SIZE];

DeviceState currentState;

char mode[7]; // Boot mode either RUN or CONFIG

const char* defaultConfigJson = R"(
{
  "network": {
    "ssid": "None",
    "password": "None",
    "useDhcp": true,
    "staticIp": "",
    "gateway": "",
    "subnet": ""
  },
  "configMode":"CONFIG"
}
)";

const char* css PROGMEM = R"css(
body { font-family: Arial, sans-serif; background-color: #f8f9fa; color: #212529; margin: 0; padding: 1rem; }
header { background-color: #007bff; color: white; padding: 1rem; text-align: center; margin-bottom: 1rem; }
header h1 { margin: 0; font-size: 2rem; color: white; }
nav { background-color: #e9ecef; padding: 0.5rem; margin-bottom: 1rem; }
nav ul { list-style-type: none; margin: 0; padding: 0; display: flex; justify-content: center; }
nav li { margin: 0 1rem; position: relative; }
nav a { color: #007bff; text-decoration: none; padding: 0.5rem; display: block; }
nav a:hover { text-decoration: underline; }
.dropdown-content { display: none; position: absolute; background-color: #f9f9f9; min-width: 160px; box-shadow: 0px 8px 16px 0px rgba(0,0,0,0.2); z-index: 1; }
.dropdown-content a { color: black; padding: 0.75rem 1rem; text-decoration: none; display: block; }
.dropdown-content a:hover { background-color: #f1f1f1; }
.dropdown:hover .dropdown-content { display: block; }
h1 { color: #007bff; }
table { width: 100%; border-collapse: collapse; margin-bottom: 1rem; }
th, td { border: 1px solid #dee2e6; padding: 0.75rem; text-align: left; }
th { background-color: #e9ecef; font-weight: bold; }
input[type="text"], textarea, select { width: 100%; padding: 0.5rem 1rem; border: 1px solid #ced4da; border-radius: 0.25rem; box-sizing: border-box; background-color: white; font-size: 1rem; line-height: 1.5; }
input[type="submit"] { background-color: #007bff; color: white; padding: 0.5rem 1rem; border: none; border-radius: 0.25rem; cursor: pointer; }
input[type="submit"]:hover { background-color: #0069d9; }
input[type="radio"], input[type="checkbox"] { margin-right: 0.5rem; }
table td:first-child, table th:first-child { width: 150px; }
.fail { background-color: red; color: white; }
.pass { background-color: green; color: white; }
label { font-weight: bold; margin-bottom: 0.5rem; display: block; }
)css";

const uint8_t database_icon_png[] PROGMEM = {
  0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0xf3, 0xff, 0x61, 0x00, 0x00, 0x00, 0xcf, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0xdd, 0x93, 0x21, 0x12, 0x83, 0x30, 0x10, 0x45, 0x5f, 0x48, 0x69, 0xd1, 0x58, 0x2e, 0x82, 0xac, 0xe1, 0x10, 0x38, 0xee, 0x81, 0xe1, 0x28, 0xdc, 0x03, 0x53, 0x01, 0xa6, 0x82, 0x41, 0xa1, 0x31, 0x08, 0x38, 0x40, 0x05, 0x4c, 0xba, 0x5d, 0x54, 0x90, 0xa6, 0x3, 0x6d, 0xd7, 0xed, 0xff, 0xb3, 0x3f, 0xfb, 0x37, 0xf3, 0x15, 0x8e, 0x2a, 0x8a, 0x42, 0x3e, 0x60, 0x6a, 0x8f, 0x79, 0x47, 0x87, 0x5d, 0xb8, 0x25, 0xe0, 0x1a, 0x76, 0xf1, 0x1f, 0x37, 0xf8, 0xa5, 0xfe, 0x40, 0xe0, 0xe4, 0x22, 0xf2, 0x3c, 0x67, 0x1c, 0x47, 0x44, 0x04, 0xad, 0x35, 0x6d, 0xdb, 0xd2, 0x75, 0x1d, 0x00, 0x2, 0xf2, 0x00, 0xee, 0x80, 0x72, 0x0a, 0x18, 0x63, 0x28, 0xcb, 0x12, 0x00, 0xdf, 0xf7, 0x49, 0xd3, 0x94, 0x65, 0x59, 0xe8, 0xfb, 0x1e, 0x60, 0x51, 0x4a, 0x5d, 0x0f, 0x5b, 0x58, 0xd7, 0x95, 0xaa, 0xaa, 0x88, 0xe3, 0xd8, 0xe2, 0x0e, 0xdf, 0x60, 0x9a, 0x26, 0xc2, 0x30, 0xb4, 0x70, 0xa7, 0x05, 0xeb, 0x25, 0xcf, 0xc3, 0x18, 0xb3, 0xb5, 0x67, 0x11, 0xb9, 0x1, 0x97, 0xc3, 0x2, 0x51, 0x14, 0x31, 0xcf, 0xf3, 0xd6, 0xfe, 0x76, 0x83, 0x20, 0x08, 0x48, 0x92, 0x84, 0xa6, 0x69, 0x2c, 0xce, 0xb9, 0x81, 0xd6, 0x9a, 0x2c, 0xcb, 0xde, 0xdf, 0x58, 0xd7, 0x35, 0xc3, 0x30, 0xec, 0x2d, 0x3c, 0xad, 0x78, 0xc2, 0xf7, 0x40, 0xed, 0x23, 0xfd, 0x2, 0xb2, 0x33, 0x54, 0x61, 0xf1, 0x24, 0x2a, 0x35, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
};
const size_t database_icon_png_len = 264;

// =====================================================================
// Function Definitions
// =====================================================================

// initHardware()
// Initializes hardware components.
// - Starts Serial communication at 115200 baud for debugging.
// - Initializes EEPROM with defined size.
// - Sets up the button with debouncing (interval 5ms, pressed state LOW).
// Call this first in setup() to prepare peripherals.

void initHardware() {
  Serial.begin(115200);
  delay(100);
  EEPROM.begin(EEPROM_SIZE);
  button.attach(BUTTON_PIN, INPUT_PULLUP);
  button.interval(5);
  button.setPressedState(LOW);
}

// initConfig()
// Loads and parses the configuration from EEPROM.
// - Calls loadConfigFromEEPROM() to read config into currentConfig.
// - Prints the loaded config to Serial.
// - Parses the config to set global variables like ssid, password, mode.
// - Sets the device hostname based on chip ID.
// Call this after initHardware() in setup().

void initConfig() {
  loadConfigFromEEPROM();
  Serial.print("Config loaded: ");
  Serial.println(currentConfig);
  parseConfig(currentConfig);
  setDeviceHostname();
}

// initWiFi()
// Initializes WiFi based on current config and mode.
// - If mode is "CONFIG" or no valid SSID, starts AP mode and returns STATE_CONFIG.
// - Otherwise, attempts to connect to the specified WiFi network.
// - Supports password-protected or open networks.
// - Timeout: 10 seconds for connection attempt.
// - On success, prints IP and returns STATE_RUN.
// - On failure, falls back to AP mode and STATE_CONFIG.
// - Applies static IP or DHCP as per config.
// Returns the determined DeviceState.
// Call this after initConfig() in setup().

DeviceState initWiFi() 
{
  Serial.println("Connecting to Wi-Fi...");
  if (strcmp(mode, "CONFIG") == 0 || strlen(ssid) == 0 || strcmp(ssid, "None") == 0) 
  {
    startAPMode();
    return STATE_CONFIG;
  } 
  else
  {
    WiFi.disconnect(true);
    delay(500);
    // Avoid parsing config again in CONFIG mode to reduce CPU load
    if (currentState != STATE_CONFIG) 
    {
      parseConfig(currentConfig);
      Serial.print("Applying config: ");
      Serial.println(currentConfig);
    }
    if (strlen(password) > 0 && strcmp(password, "None") != 0) 
    {
      WiFi.begin(ssid, password);
    }
    else 
    {
      WiFi.begin(ssid);
    }

    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      delay(250);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) 
    {
      IPAddress localIP = WiFi.localIP();
      char ipBuf[16];
      snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", localIP[0], localIP[1], localIP[2], localIP[3]);
      Serial.print("\nConnected: ");
      Serial.println(ipBuf);
      return STATE_RUN;
    }
    else 
    {
      startAPMode();
      return STATE_CONFIG;
    }
  }
}

// startAPMode()
// Starts the device in Access Point (AP) mode for configuration.
// - Generates AP SSID via setAPSSID() (e.g., "ESP01_AP_XXXXXX" where XXXXXX is chip ID hex).
// - Sets WiFi mode to 802.11g for compatibility.
// - Sets max output power (20.5 dBm) for better range.
// - Starts soft AP on channel 6, visible SSID.
// - Prints AP IP (usually 192.168.4.1) to Serial.
// Call this when entering CONFIG mode.

void startAPMode() {
  Serial.println("Starting AP mode...");
  setAPSSID();
  WiFi.setPhyMode(WIFI_PHY_MODE_11G); // Use 802.11g for better compatibility
  WiFi.setOutputPower(20.5); // Max transmit power (20.5 dBm) for stable signal
  WiFi.softAP(ap_ssid, ap_password, 6, 0); // Channel 6, SSID visible
  IPAddress apIP = WiFi.softAPIP();
  char ipBuf[16];
  snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", apIP[0], apIP[1], apIP[2], apIP[3]);
  Serial.println(ipBuf);
}

// initWebServer()
// Sets up the web server.
// - Calls configureWebServerRoutes() to define HTTP handlers.
// - Starts the server.
// - Prints confirmation to Serial.
// Call this after initWiFi() in setup(). The server runs in both modes but is primarily for CONFIG.

void initWebServer() {
  configureWebServerRoutes();
  server.begin();
  Serial.println("Web server started.");
}

// handleButton()
// Handles button input in the loop().
// - Uses debouncing via Bounce2.
// - Short press (>2s <20s): Toggles between RUN and CONFIG modes, saves to EEPROM, restarts.
// - Long press (>=20s): Performs factory reset.
// Call this repeatedly in loop() for button monitoring.

void handleButton() {
  button.update();
  static unsigned long pressStartTime = 0;
  if (button.pressed()) {
    pressStartTime = millis();
  }
  if (button.released()) {
    unsigned long duration = millis() - pressStartTime;
    if (duration > 2000 && duration < 20000) {
      // Short press over 2 seconds: Toggle mode
      Serial.println("Short press detected (over 2s), toggling mode...");
      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, currentConfig);
      if (!error) {
        const char* currentMode = doc["configMode"] | "RUN";
        const char* newMode = strcmp(currentMode, "CONFIG") == 0 ? "RUN" : "CONFIG";
        doc["configMode"] = newMode;
        char newJson[EEPROM_SIZE];
        serializeJson(doc, newJson, sizeof(newJson));
        Serial.print("New config JSON: ");
        Serial.println(newJson);
        saveConfigToEEPROM(newJson);
        strcpy(currentConfig, newJson);
        Serial.println("Mode toggled, restarting...");
        ESP.restart();
      } 
      else 
      {
        Serial.print("JSON parse error in button toggle: ");
        Serial.println(error.c_str());
      }
    }
  }
  if (button.isPressed() && millis() - pressStartTime >= 20000) {
    performFactoryReset();
  }
}

// loadConfigFromEEPROM()
// Loads the JSON config from EEPROM into currentConfig buffer.
// - Reads bytes until null terminator or EEPROM_SIZE.
// - If EEPROM is all 0xFF (empty) or invalid (not starting with '{'), applies defaultConfigJson and saves it.
// - Prints loaded or default config to Serial.
// Call this in initConfig().

void loadConfigFromEEPROM() 
{
  EEPROM.begin(EEPROM_SIZE);
  int i = 0;
  bool allFF = true;
  for (; i < EEPROM_SIZE - 1; i++) {
    char c = EEPROM.read(i);
    currentConfig[i] = c;
    if (c != 0xFF) allFF = false;
    if (c == 0) break;
  }
  currentConfig[i] = 0;
  if (allFF || i == 0 || currentConfig[0] != '{') {
    strcpy(currentConfig, defaultConfigJson);
    saveConfigToEEPROM(currentConfig);
    Serial.println("EEPROM empty or invalid; applied and saved default config.");
  } else {
    Serial.println("Loaded config from EEPROM:");
    Serial.println(currentConfig);
  }
  EEPROM.end();
}

// saveConfigToEEPROM(const char* newConfig)
// Saves the provided JSON string to EEPROM.
// - Writes characters up to null terminator or EEPROM_SIZE-1.
// - Commits changes and ends EEPROM session.
// - Prints confirmation to Serial.
// Call this whenever config changes (e.g., from web interface or button).

void saveConfigToEEPROM(const char* newConfig) {
  EEPROM.begin(EEPROM_SIZE);
  int len = strlen(newConfig);
  if (len > EEPROM_SIZE - 1) len = EEPROM_SIZE - 1;
  for (int i = 0; i < len; i++) {
    EEPROM.write(i, newConfig[i]);
  }
  EEPROM.write(len, 0);
  EEPROM.commit();
  EEPROM.end();
  Serial.println("Saved config to EEPROM.");
}

// setAPSSID()
// Generates a unique AP SSID based on the ESP's chip ID.
// - Format: "ESP01_AP_%X" where %X is the hex chip ID.
// - Ensures uniqueness across devices.
// Called in startAPMode().

void setAPSSID() 
{
  uint32_t chipID = ESP.getChipId();
  snprintf(ap_ssid, sizeof(ap_ssid), "ESP01_AP_%X", chipID);
}

// setDeviceHostname()
// Sets the device's hostname for mDNS or network identification.
// - Format: "ESP01_%X" where %X is hex chip ID.
// - Prints to Serial.
// Called in initConfig().

void setDeviceHostname() 
{
  uint32_t chipID = ESP.getChipId();
  char hostname[20];
  snprintf(hostname, sizeof(hostname), "ESP01_%X", chipID);
  WiFi.setHostname(hostname);
  Serial.print("Hostname set to: ");
  Serial.println(hostname);
}

// parseConfig(const char* jsonConfig)
// Parses the JSON config string and applies settings.
// - Uses ArduinoJson to deserialize.
// - Extracts network settings: ssid, password, useDhcp, staticIp, gateway, subnet.
// - Extracts configMode into mode.
// - If not DHCP, parses and sets static IP config using WiFi.config().
// - Falls back to DHCP on invalid IP strings.
// - Prints actions to Serial.
// Called after loading config or changes.

void parseConfig(const char* jsonConfig)
{
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonConfig);
  if (error) {
    Serial.println("Failed to parse config JSON");
    return;
  }
  JsonObject netObj = doc["network"];
  strlcpy(ssid, netObj["ssid"] | "", sizeof(ssid));
  strlcpy(password, netObj["password"] | "", sizeof(password));
  bool useDhcp = netObj["useDhcp"] | true;
  const char* staticIpStr = netObj["staticIp"] | "";
  const char* gatewayStr = netObj["gateway"] | "";
  const char* subnetStr = netObj["subnet"] | "";
  strlcpy(mode, doc["configMode"] | "RUN", sizeof(mode));

  IPAddress ip, gw, sn;
  if (!useDhcp) {
    if (ip.fromString(staticIpStr) && gw.fromString(gatewayStr) && sn.fromString(subnetStr)) {
      WiFi.config(ip, gw, sn);
      char ipBuf[16];
      snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
      Serial.print("Static IP set: ");
      Serial.println(ipBuf);
    } else {
      Serial.println("Invalid IP settings, falling back to DHCP");
    }
  } else {
    WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
    Serial.println("Using DHCP");
  }
}

// performFactoryReset()
// Resets EEPROM to all 0xFF (erased state).
// - Writes 0xFF to all bytes in EEPROM_SIZE.
// - Commits and ends EEPROM.
// - Prints to Serial and restarts ESP.
// Called on long button press or from web interface.

void performFactoryReset() 
{
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0xFF);
  }
  EEPROM.commit();
  EEPROM.end();
  Serial.println("Factory reset executed. Restarting...");
  delay(500);
  ESP.restart();
}

// =====================================================================
// Web Handler Functions
// =====================================================================
// These functions handle HTTP requests for the web interface.
// - Use server.send() to respond with HTML.
// - POST methods process form data, update config, redirect.
// - Content is sent in chunks using sendContent() to manage memory.
// - Access via browser when in CONFIG mode (or RUN if connected).

// handleRoot()
// Handles GET/POST to root "/".
// - In CONFIG: Redirects to /network.
// - In RUN: Shows basic home page.
// Extend this for your project's dashboard.

void handleRoot() {
  if (currentState == STATE_CONFIG) {
    server.sendHeader("Location", "/network");
    server.send(303);
    return;
  }
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  sendHtmlHeader("Home");
  server.sendContent(F("<h1>ESP01 Web Manager</h1>"));
  sendHtmlFooter();
  server.sendContent("");
}

// handleRestart()
// Handles GET/POST to /restart.
// - GET: Shows form with radio options: Reboot, to RUN, to CONFIG.
// - POST: Processes action, updates configMode if needed, saves, restarts.

void handleRestart() {
  if (server.method() == HTTP_POST) {
    String action = server.arg("action");
    bool needSave = false;
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, currentConfig);
    if (!error) {
      const char* currentMode = doc["configMode"] | "RUN";
      if (action == "run" && strcmp(currentMode, "RUN") != 0) {
        doc["configMode"] = "RUN";
        needSave = true;
      } else if (action == "config" && strcmp(currentMode, "CONFIG") != 0) {
        doc["configMode"] = "CONFIG";
        needSave = true;
      }
      if (needSave) {
        char newJson[EEPROM_SIZE];
        serializeJson(doc, newJson, sizeof(newJson));
        saveConfigToEEPROM(newJson);
        strcpy(currentConfig, newJson);
      }
    }
    server.send(200, "text/html", "<p>Restarting...</p>");
    delay(500);
    ESP.restart();
    return;
  }
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  sendHtmlHeader("Restart");
  server.sendContent(F("<h1>Restart ESP</h1>"
                       "<form method='POST'>"
                       "<label><input type='radio' name='action' value='reboot' checked> Reboot</label><br>"
                       "<label><input type='radio' name='action' value='run'> Reboot to RUN</label><br>"
                       "<label><input type='radio' name='action' value='config'> Reboot to Config</label><br>"
                       "<input type='submit' value='Execute'>"
                       "</form>"));
  sendHtmlFooter();
  server.sendContent("");
}

// handleFactoryReset()
// Handles GET/POST to /factoryreset.
// - GET: Shows confirmation form.
// - POST: Calls performFactoryReset().

void handleFactoryReset() {
  if (server.method() == HTTP_POST) {
    performFactoryReset();
    return;
  }
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  sendHtmlHeader("Factory Reset");
  server.sendContent(F("<h1>Reset to Factory</h1>"
                       "<form method='POST' onsubmit='return confirm(\"Are you sure?\");'>"
                       "<input type='submit' value='Reset to Factory'></form>"));
  sendHtmlFooter();
  server.sendContent("");
}

// handleJsonEditor()
// Handles GET/POST to /jsonedit.
// - GET: Shows textarea with currentConfig for editing.
// - POST: Saves new JSON from form, updates currentConfig, parses, redirects.

void handleJsonEditor() {
  if (server.method() == HTTP_POST) {
    if (server.hasArg("jsondata")) {
      char newConfig[EEPROM_SIZE];
      server.arg("jsondata").toCharArray(newConfig, EEPROM_SIZE);
      saveConfigToEEPROM(newConfig);
      strcpy(currentConfig, newConfig);
      parseConfig(currentConfig);
    }
    server.sendHeader("Location", "/jsonedit");
    server.send(303);
    return;
  }
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  sendHtmlHeader("JSON Editor");
  server.sendContent(F("<h1>JSON Editor</h1>"
                       "<form method='POST' action='/jsonedit'>"
                       "<textarea name='jsondata' rows='15' cols='50'>"));
  server.sendContent(currentConfig);
  server.sendContent(F("</textarea><br>"
                       "<input type='submit' value='Save'>"
                       "</form>"));
  sendHtmlFooter();
  server.sendContent("");
}

// handleNetworkConfig()
// Handles GET/POST to /network.
// - GET: Parses current config, shows form with current values for SSID, password, DHCP/static, IPs.
// - POST: Updates network section in JSON, saves, parses, redirects.
// Use this to configure WiFi settings via web.

void handleNetworkConfig() {
  if (server.method() == HTTP_POST) {
    char newSsid[32];
    char newPassword[64];
    bool useDhcpBool = server.arg("useDhcp") == "1";
    char staticIp[16];
    char gateway[16];
    char subnet[16];
    server.arg("ssid").toCharArray(newSsid, sizeof(newSsid));
    server.arg("password").toCharArray(newPassword, sizeof(newPassword));
    server.arg("staticIp").toCharArray(staticIp, sizeof(staticIp));
    server.arg("gateway").toCharArray(gateway, sizeof(gateway));
    server.arg("subnet").toCharArray(subnet, sizeof(subnet));

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, currentConfig);
    if (error) {
      Serial.println("Failed to parse config JSON");
      server.send(500, "text/html", "Error parsing config");
      return;
    }
    JsonObject netObj = doc["network"];
    netObj["ssid"] = newSsid;
    netObj["password"] = newPassword;
    netObj["useDhcp"] = useDhcpBool;
    netObj["staticIp"] = staticIp;
    netObj["gateway"] = gateway;
    netObj["subnet"] = subnet;
    serializeJson(doc, currentConfig, sizeof(currentConfig));
    saveConfigToEEPROM(currentConfig);
    parseConfig(currentConfig);
    server.sendHeader("Location", "/network");
    server.send(303);
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, currentConfig);
  if (error) {
    Serial.println("Failed to parse config JSON");
    server.send(500, "text/html", "Error parsing config");
    return;
  }
  JsonObject netObj = doc["network"];
  char currSsid[32];
  char currPassword[64];
  bool currUseDhcp = netObj["useDhcp"] | true;
  char currStaticIp[16];
  char currGateway[16];
  char currSubnet[16];
  strlcpy(currSsid, netObj["ssid"] | "a", sizeof(currSsid));
  strlcpy(currPassword, netObj["password"] | "", sizeof(currPassword));
  strlcpy(currStaticIp, netObj["staticIp"] | "", sizeof(currStaticIp));
  strlcpy(currGateway, netObj["gateway"] | "", sizeof(currGateway));
  strlcpy(currSubnet, netObj["subnet"] | "", sizeof(currSubnet));

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  sendHtmlHeader("Network Config");
  server.sendContent(F("<h1>Network Config</h1>"
                       "<form method='POST' action='/network'>"
                       "<table>"
                       "<tr><th>SSID</th><td>"));
  char buf[256];
  snprintf(buf, sizeof(buf), "<input type='text' name='ssid' value='%s'>", currSsid);
  server.sendContent(buf);
  server.sendContent(F("</td></tr>"
                       "<tr><th>Password</th><td>"));
  snprintf(buf, sizeof(buf), "<input type='text' name='password' value='%s'>", currPassword);
  server.sendContent(buf);
  server.sendContent(F("</td></tr>"
                       "<tr><th>IP Settings</th><td>"));
  const char* dhcpChecked = currUseDhcp ? " checked='checked'" : "";
  const char* staticChecked = !currUseDhcp ? " checked='checked'" : "";
  snprintf(buf, sizeof(buf), "<input type='radio' name='useDhcp' value='1'%s> DHCP "
                             "<input type='radio' name='useDhcp' value='0'%s> Static", dhcpChecked, staticChecked);
  server.sendContent(buf);
  server.sendContent(F("</td></tr>"
                       "<tr><th>Static IP</th><td>"));
  snprintf(buf, sizeof(buf), "<input type='text' name='staticIp' value='%s'>", currStaticIp);
  server.sendContent(buf);
  server.sendContent(F("</td></tr>"
                       "<tr><th>Gateway</th><td>"));
  snprintf(buf, sizeof(buf), "<input type='text' name='gateway' value='%s'>", currGateway);
  server.sendContent(buf);
  server.sendContent(F("</td></tr>"
                       "<tr><th>Subnet</th><td>"));
  snprintf(buf, sizeof(buf), "<input type='text' name='subnet' value='%s'>", currSubnet);
  server.sendContent(buf);
  server.sendContent(F("</td></tr>"
                       "</table>"
                       "<br><input type='submit' value='Save'>"
                       "</form>"));
  sendHtmlFooter();
  server.sendContent("");

  Serial.print("Heap before sending: ");
  Serial.println(ESP.getFreeHeap());
  server.client().flush();
}

// configureWebServerRoutes()
// Sets up all HTTP routes for the web server.
// - Maps URLs to handler functions.
// - Includes root, restart, factory reset, JSON editor, network config, favicon.
// Add more server.on() calls here for custom routes.

void configureWebServerRoutes() 
{
  server.on("/", handleRoot);
  server.on("/restart", handleRestart);
  server.on("/factoryreset", handleFactoryReset);
  server.on("/jsonedit", handleJsonEditor);
  server.on("/network", handleNetworkConfig);
  server.on("/favicon.ico", handleFavicon);  // Serve favicon
}

// sendHtmlHeader(const char* title)
// Sends common HTML header with title, meta, CSS, body start, header, navigation menu.
// - Uses sendContent() for chunked sending.
// - Navigation includes dropdown for config options.
// Called at the start of most handler responses.

void sendHtmlHeader(const char* title) {
  server.sendContent(F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                       "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                       "<title>"));
  server.sendContent(title);
  server.sendContent(F("</title><style>"));
  server.sendContent_P(css);
  server.sendContent(F("</style></head><body>"
                       "<header><h1>ESP01 Web Template</h1></header>"
                       "<nav>"
                       "<ul>"
                       "<li><a href='/'>Home</a></li>"));
  server.sendContent(F("<li class=\"dropdown\">"
                       "<a href='javascript:void(0)'>Config</a>"
                       "<div class=\"dropdown-content\">"
                       "<a href='/network'>Network Config</a>"
                       "<a href='/jsonedit'>Json Edit</a>"
                       "<a href='/restart'>Restart</a>"
                       "<a href='/factoryreset'>Reset to Factory</a>"
                       "</div></li>"
                       "</ul></nav>"));
}

// sendHtmlFooter()
// Sends HTML footer with copyright and closes body/html.
// Called at the end of handler responses.

void sendHtmlFooter() 
{
  server.sendContent(F("<hr><p>© 2025 ESP01 Web Template</p></body></html>"));
}

// handleFavicon()
// Serves the favicon.ico as PNG from PROGMEM.
// - Responds with 200 and image data.

void handleFavicon() 
{
  server.send_P(200, "image/png", (const char*)database_icon_png, database_icon_png_len);
}


//=====================================================================
// Setup
// =====================================================================
// setup()
// Arduino setup function, runs once on boot.
// - Initializes hardware, config, WiFi, web server.
// - Determines currentState based on WiFi init.

void setup() 
{
  initHardware();
  initConfig();
  currentState = initWiFi();
  initWebServer();
}

// =====================================================================
// Loop
// =====================================================================
// loop()
// Main Arduino loop, runs repeatedly.
// - Handles web server clients.
// - Handles button input.
// - Based on currentState:
//   - STATE_RUN: Place your normal application code here (e.g., sensor reading, MQTT).
//   - STATE_CONFIG: Runs config-specific code (currently empty; add if needed).
// - Delay 10ms for WiFi processing.

void loop() {
  server.handleClient();
  handleButton();

  if (currentState == STATE_RUN) 
  {
    // This is the section of the loop that run normally
  }
  else if (currentState == STATE_CONFIG) 
  {
    // This runs in config mode
  }

  delay(10); // Increased delay for more WiFi stack processing time
}