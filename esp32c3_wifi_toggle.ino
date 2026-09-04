#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>

// ---------- Pin / timing config ----------
const int BOOT_BUTTON_PIN = 9;      // GPIO9 on most ESP32-C3 dev boards
const int LIGHT_SENS_PIN = A0; 
const int TOGGLE_PIN = 21; 

const int LONG_PRESS_MS = 10000; // 10 seconds to enter setup mode
const int DEBOUNCE_MS = 50;
const int SERVO_ACTIVE_MS = 250;
const int SERVO_INACTIVE_MS = 1000;

const int LIGHT_SENS_THRESHOLD = 2000;
const int TOGGLE_PRE_MS = 250;
const int TOGGLE_POST_MS = 10000;

const char* AP_SSID = "PrinterESP-Setup"; // Name of the temporary setup network
const char* MDNS_NAME = "printeresp";      // Reachable at http://printeresp.local/

// ---------- Globals ----------
Preferences prefs;
WebServer server(80);

bool state = false;

bool buttonPressed = false;
unsigned long pressStartTime = 0;
bool lastRawState = HIGH;
unsigned long lastDebounceTime = 0;

bool toggleActive = false;
unsigned long lastToggleAction = 0;

// ---------------------------------------------------------------------------
// Hardware Handlers
// ---------------------------------------------------------------------------
// Get whether printer is actually on with a light sensor
bool getState(){
  return analogRead(LIGHT_SENS_PIN) > LIGHT_SENS_THRESHOLD;
}

// Interact with the power button
void updateOutput(){
  if(((getState() != state) || toggleActive) && (lastToggleAction + (toggleActive ? TOGGLE_PRE_MS : TOGGLE_POST_MS) < millis())){
    toggleActive = !toggleActive;
    digitalWrite(TOGGLE_PIN, toggleActive);
    lastToggleAction = millis();
  }
}

// ---------------------------------------------------------------------------
// Normal-mode web handlers
// ---------------------------------------------------------------------------
void handleOn() {
  state = true;
  server.send(200, "text/plain", "");
}

void handleOff() {
  state = false;
  server.send(200, "text/plain", "");
}

void handleStatus() {
  server.send(200, "text/plain", getState() ? "true" : "false");
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><body>";
  html += "<h1>ESP32-C3 Control</h1>";
  html += "<button onclick=\"fetch('/on')\">ON</button> ";
  html += "<button onclick=\"fetch('/off')\">OFF</button> ";
  html += "<button onclick=\"fetch('/status').then(r=>r.text()).then(t=>alert('Status: '+t))\">STATUS</button>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// ---------------------------------------------------------------------------
// Setup-mode (WiFi provisioning) — blocks here until credentials are saved
// ---------------------------------------------------------------------------
void enterSetupMode() {
  Serial.println("Entering WiFi setup mode...");

  // Tear down any existing server/wifi state
  server.stop();
  WiFi.disconnect(true);
  delay(100);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID); // Open network, no password. Add a password as a 2nd arg if you want one.
  Serial.print("AP started. Connect to \"");
  Serial.print(AP_SSID);
  Serial.println("\" and browse to http://192.168.4.1/");

  // Quick scan so the config page can show a dropdown of nearby networks
  int networkCount = WiFi.scanNetworks();

  WebServer setupServer(80);

  setupServer.on("/", HTTP_GET, [&networkCount, &setupServer]() {
    String html = "<!DOCTYPE html><html><body>";
    html += "<h1>WiFi Setup</h1>";
    html += "<form action='/save' method='POST'>";
    html += "SSID:<br><input list='nets' name='ssid'><datalist id='nets'>";
    for (int i = 0; i < networkCount; i++) {
      html += "<option value='" + WiFi.SSID(i) + "'>";
    }
    html += "</datalist><br><br>";
    html += "Password:<br><input type='password' name='pass'><br><br>";
    html += "<input type='submit' value='Save & Connect'>";
    html += "</form></body></html>";
    setupServer.send(200, "text/html", html);
  });

  setupServer.on("/save", HTTP_POST, [&setupServer]() {
    String newSsid = setupServer.arg("ssid");
    String newPass = setupServer.arg("pass");

    prefs.begin("wifi", false);
    prefs.putString("ssid", newSsid);
    prefs.putString("pass", newPass);
    prefs.end();

    setupServer.send(200, "text/html",
      "<html><body><h1>Saved. Rebooting...</h1></body></html>");
    delay(1500);
    ESP.restart();
  });

  setupServer.begin();

  // Block here forever, serving the config page, until the user submits
  // credentials (which triggers ESP.restart() above).
  while (true) {
    setupServer.handleClient();
    delay(2);
  }
}

// ---------------------------------------------------------------------------
// Attempt to connect using stored credentials
// ---------------------------------------------------------------------------
bool connectWithStoredCredentials() {
  prefs.begin("wifi", true); // read-only
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  if (ssid.length() == 0) {
    Serial.println("No stored WiFi credentials.");
    return false;
  }

  Serial.print("Connecting to stored network: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  return WiFi.status() == WL_CONNECTED;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  analogReadResolution(12);

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LIGHT_SENS_PIN, INPUT);
  pinMode(TOGGLE_PIN, OUTPUT);

  if (!connectWithStoredCredentials()) {
    enterSetupMode(); // Blocks, then restarts the device once configured
  }

  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin(MDNS_NAME)) {
    Serial.print("mDNS responder started: http://");
    Serial.print(MDNS_NAME);
    Serial.println(".local/");
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("Error starting mDNS");
  }

  server.on("/", handleRoot);
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  server.on("/status", handleStatus);
  server.begin();
  Serial.println("HTTP server started");
}

// ---------------------------------------------------------------------------
// Boot button handling: short press = toggle, 10s hold = setup mode
// ---------------------------------------------------------------------------
void handleButton() {
  bool raw = digitalRead(BOOT_BUTTON_PIN); // LOW = pressed

  if (raw != lastRawState) {
    lastDebounceTime = millis();
    lastRawState = raw;
  }

  if (millis() - lastDebounceTime > DEBOUNCE_MS) {
    bool pressedNow = (raw == LOW);

    if (pressedNow && !buttonPressed) {
      // Just pressed
      buttonPressed = true;
      pressStartTime = millis();
    } else if (!pressedNow && buttonPressed) {
      // Just released -> short press, toggle if it wasn't already a long hold
      unsigned long heldFor = millis() - pressStartTime;
      buttonPressed = false;
      if (heldFor < LONG_PRESS_MS) {
        state = !state;
        Serial.print("Toggled state to: ");
        Serial.println(state ? "true" : "false");
      }
    } else if (pressedNow && buttonPressed) {
      // Still holding — check for long-press threshold
      if (millis() - pressStartTime >= LONG_PRESS_MS) {
        Serial.println("Long press detected -> entering setup mode");
        buttonPressed = false; // prevent re-trigger
        enterSetupMode();      // blocks until reconfigured, then restarts
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------
void loop() {
  server.handleClient();
  handleButton();
  updateOutput();
}
