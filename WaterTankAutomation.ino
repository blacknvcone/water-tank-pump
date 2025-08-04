#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <ESP8266httpUpdate.h>

// =======================
// === GPIO PIN DEFINITIONS ===
// =======================
#define LOW_SENSOR_PIN 4  // D2
#define HIGH_SENSOR_PIN 5 // D1
#define RELAY_PIN 14      // D5
#define LED_PIN 2         // D4 (Built-in LED, active low)

// =========================
// === EEPROM ADDRESSES ===
// =========================
#define EEPROM_SIZE 256
#define WIFI_SSID_ADDR 100
#define WIFI_PASS_ADDR 140
#define MQTT_ADDR 0
#define OTA_PASS_ADDR 200 // New address for OTA password

// ========================
// === GLOBAL OBJECTS ===
// ========================
WiFiManager wifiManager;
WiFiClient espClient;
PubSubClient client(espClient);
ESP8266WebServer server(80);

// ===============================
// === CONFIGURATION VARIABLES ===
// ===============================
char wifi_ssid[40] = "";
char wifi_password[40] = "";
char mqtt_server[40] = "";
char mqtt_user[20] = "";
char mqtt_password[20] = "";
int mqtt_port = 1883;
char ota_password[20] = "ota_password"; // Default OTA password

// ======================================
// === ZIGBEE2MQTT SPECIFIC VARIABLES ===
// ======================================
#define DEVICE_ID "water_tank_controller"
#define MQTT_STATE_TOPIC "zigbee2mqtt/" DEVICE_ID
#define MQTT_COMMAND_TOPIC "zigbee2mqtt/" DEVICE_ID "/set"
#define MQTT_AVAILABILITY_TOPIC "zigbee2mqtt/bridge/state"

// ============================
// === DEVICE STATE VARIABLES ===
// ============================
bool overrideMode = false;
bool overrideState = false; // true = ON, false = OFF
bool apMode = false;
bool mqttConfigured = false;

// Pump and sensor states
bool pumpState = false; // true = ON, false = OFF
bool lastPumpState = false; // To track changes for publishing
bool lowSensorState = false;
bool highSensorState = false;
bool lastLowSensorState = false;
bool lastHighSensorState = false;

unsigned long lastStatusUpdate = 0;
const long statusUpdateInterval = 10000; // Publish full state every 10 seconds

// =======================
// === FUNCTION PROTOTYPES ===
// =======================
void setupWebServer();
void loadConfig();
void saveConfig();
bool readSensor(int pin);
void handlePumpLogic();
void handleLED();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void reconnectMQTT();
void publishState();
void publishPumpStatus();
void publishSensorStatus();

// ===================
// === SETUP FUNCTION ===
// ===================
void setup() {
  Serial.begin(115200);
  
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Initially OFF

  pinMode(LOW_SENSOR_PIN, INPUT);
  pinMode(HIGH_SENSOR_PIN, INPUT);
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // LED OFF (active low)
  
  loadConfig();
  
  WiFi.begin(wifi_ssid, wifi_password);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.softAP("WaterTank-Setup");
    apMode = true;
    Serial.println("\nAP Mode Started");
  } else {
    digitalWrite(LED_PIN, LOW); // LED ON when connected
  }
  
  if (mqttConfigured) {
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(mqttCallback);
  }
  
  setupWebServer();
  server.begin();
}

// ==================
// === MAIN LOOP ===
// ==================
void loop() {
  if (!apMode && WiFi.status() == WL_CONNECTED) {
    if (mqttConfigured) {
      if (!client.connected()) {
        reconnectMQTT();
      }
      client.loop();
      
      // Periodically send state updates to Z2M
      if (millis() - lastStatusUpdate > statusUpdateInterval) {
        publishState();
        lastStatusUpdate = millis();
      }
    }
  }
  
  server.handleClient();
  handleLED();
  handlePumpLogic();
}

// ==========================
// === WEB SERVER FUNCTIONS ===
// ==========================
void setupWebServer() {
  // Main status page
  server.on("/", HTTP_GET, []() {
    String html = "<html><body>"
                  "<script>"
                  "function fetchStatus() {"
                  "fetch('/')"
                  ".then(response => response.text())"
                  ".then(html => { document.body.innerHTML = html; });"
                  "}"
                  "setInterval(fetchStatus, 2000);"
                  "</script>"
                  "<h3>Controller Status</h3>"
                  "<table border='1'><tr><th>Item</th><th>Status</th></tr>"
                  "<tr><td>WiFi Connected</td><td>" + String(WiFi.status() == WL_CONNECTED ? "Yes" : "No") + "</td></tr>"
                  "<tr><td>MQTT Connected</td><td>" + String(client.connected() ? "Yes" : "No") + "</td></tr>"
                  "<tr><td>Low Sensor</td><td>" + String(lowSensorState ? "Active" : "Inactive") + "</td></tr>"
                  "<tr><td>High Sensor</td><td>" + String(highSensorState ? "Active" : "Inactive") + "</td></tr>"
                  "<tr><td>Pump Status</td><td>" + String(pumpState ? "ON" : "OFF") + "</td></tr>"
                  "<tr><td>Control Mode</td><td>" + String(overrideMode ? "Manual" : "Automatic") + "</td></tr>"
                  "</table>"
                  "<br><a href='/setup'>Configure Settings</a>"
                  "</body></html>";
    server.send(200, "text/html", html);
  });

  // Configuration setup page
  server.on("/setup", HTTP_GET, []() {
    String html = "<html><body>"
                  "<h3>Configuration Setup</h3><form action='/save' method='post'>"
                  "<table border='1'><tr><th>Setting</th><th>Value</th></tr>"
                  "<tr><td>SSID</td><td><input type='text' name='ssid' value='" + String(wifi_ssid) + "'></td></tr>"
                  "<tr><td>Password</td><td><input type='password' name='wifipass' value='" + String(wifi_password) + "'></td></tr>"
                  "<tr><td>MQTT Server</td><td><input type='text' name='server' value='" + String(mqtt_server) + "'></td></tr>"
                  "<tr><td>MQTT Port</td><td><input type='number' name='port' value='" + String(mqtt_port) + "'></td></tr>"
                  "<tr><td>Username</td><td><input type='text' name='user' value='" + String(mqtt_user) + "'></td></tr>"
                  "<tr><td>Password</td><td><input type='password' name='pass' value='" + String(mqtt_password) + "'></td></tr>"
                  "<tr><td>OTA Password</td><td><input type='password' name='otapass' value='" + String(ota_password) + "'></td></tr>"
                  "</table><input type='submit' value='Save'></form>"
                  "<br><a href='/ota'>OTA Update</a>"
                  "</body></html>";
    server.send(200, "text/html", html);
  });

  // Save configuration handler
  server.on("/save", HTTP_POST, []() {
    String newSSID = server.arg("ssid");
    String newPass = server.arg("wifipass");
    String newMQTT = server.arg("server");
    String newUser = server.arg("user");
    String newPassMQTT = server.arg("pass");
    String newOTAPass = server.arg("otapass");
    int newPort = server.arg("port").toInt();

    if (newSSID.length() > 0) newSSID.toCharArray(wifi_ssid, sizeof(wifi_ssid));
    if (newPass.length() > 0) newPass.toCharArray(wifi_password, sizeof(wifi_password));
    if (newMQTT.length() > 0) newMQTT.toCharArray(mqtt_server, sizeof(mqtt_server));
    if (newUser.length() > 0) newUser.toCharArray(mqtt_user, sizeof(mqtt_user));
    if (newPassMQTT.length() > 0) newPassMQTT.toCharArray(mqtt_password, sizeof(mqtt_password));
    if (newOTAPass.length() > 0) newOTAPass.toCharArray(ota_password, sizeof(ota_password));
    if (newPort > 0) mqtt_port = newPort;

    saveConfig();

    server.send(200, "text/html", "<html><body><h3>Settings Saved!</h3><a href='/'>Go Back</a></body></html>");
    
    delay(2000);
    ESP.restart(); // Restart to apply new settings
  });
  
  // OTA Update page
  server.on("/ota", HTTP_GET, []() {
    String html = "<html><body>"
                  "<h3>OTA Update</h3>"
                  "<form method='POST' action='/ota_update' enctype='multipart/form-data'>"
                  "OTA Password:<br><input type='password' name='password'><br>"
                  "Upload Firmware File (.bin):<br><input type='file' name='update'><br>"
                  "<input type='submit' value='Update'>"
                  "</form></body></html>";
    server.send(200, "text/html", html);
  });

  // OTA update handler
  server.on("/ota_update", HTTP_POST, []() {
    bool authenticated = false;
    if (server.hasArg("password")) {
      String providedPass = server.arg("password");
      if (providedPass == String(ota_password)) {
        authenticated = true;
      }
    }

    if (!authenticated) {
      server.send(401, "text/plain", "Authentication Failed");
      return;
    }
    
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("OTA Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("OTA Update Success: %u bytes\n", upload.totalSize);
        server.send(200, "text/html", "<h1>OTA Update Success! Rebooting...</h1>");
        delay(1000);
        ESP.restart();
      } else {
        Update.printError(Serial);
        server.send(500, "text/html", "<h1>OTA Update Failed!</h1><p>Check serial monitor for details.</p>");
      }
    }
  });
}

// ========================
// === SENSOR FUNCTIONS ===
// ========================
bool readSensor(int pin) {
  int activeCount = 0;
  for (int i = 0; i < 5; i++) {
    if (digitalRead(pin) == HIGH) {
      activeCount++;
    }
    delay(10);
  }
  return activeCount >= 3;
}

// ===========================
// === APPLICATION LOGIC ===
// ===========================
void handlePumpLogic() {
  lowSensorState = readSensor(LOW_SENSOR_PIN);
  highSensorState = readSensor(HIGH_SENSOR_PIN);

  // Check for sensor state changes and publish to MQTT if needed
  if (lowSensorState != lastLowSensorState || highSensorState != lastHighSensorState) {
    publishSensorStatus();
    lastLowSensorState = lowSensorState;
    lastHighSensorState = highSensorState;
  }

  if (!overrideMode) {
    if (highSensorState) {  
      pumpState = false; // Turn OFF the pump
    } 
    else if (!lowSensorState) {  
      pumpState = true; // Turn ON the pump
    } 
  } else {
    // Manual Override Mode
    pumpState = overrideState;
  }
  
  // Apply pump state and check for changes
  digitalWrite(RELAY_PIN, pumpState ? HIGH : LOW);
  if (pumpState != lastPumpState) {
    publishPumpStatus();
    lastPumpState = pumpState;
  }
}

void handleLED() {
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_PIN, LOW);
  } else {
    static unsigned long lastBlinkTime = 0;
    static bool ledState = false;

    if (millis() - lastBlinkTime >= 500) {
      lastBlinkTime = millis();
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    }
  }
}

// =======================
// === MQTT FUNCTIONS ===
// =======================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (String(topic) == MQTT_COMMAND_TOPIC) {
    StaticJsonDocument<256> doc;
    deserializeJson(doc, payload, length);
    
    if (doc.containsKey("state")) {
      String command = doc["state"].as<String>();
      if (command == "ON") {
        overrideMode = true;
        overrideState = true;
        Serial.println("MQTT Command: Manual ON");
      } else if (command == "OFF") {
        overrideMode = true;
        overrideState = false;
        Serial.println("MQTT Command: Manual OFF");
      }
    }
    
    if (doc.containsKey("mode")) {
      String command = doc["mode"].as<String>();
      if (command == "AUTO") {
        overrideMode = false;
        Serial.println("MQTT Command: AUTO mode");
      }
    }
    
    // Publish the updated state back to Z2M
    publishState();
  }
}

void reconnectMQTT() {
  while (!client.connected() && mqttConfigured) {
    Serial.println("Attempting MQTT connection...");
    
    String clientId = "ESP8266Client-" + String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      Serial.println("✅ MQTT Connected!");
      client.subscribe(MQTT_COMMAND_TOPIC);
      client.publish(MQTT_AVAILABILITY_TOPIC, "online");
      publishState();
      return;
    } else {
      Serial.print("❌ Failed, rc=");
      Serial.print(client.state());
      Serial.println(" -> Retrying in 5 seconds...");
      delay(5000);
    }
  }
}

void publishState() {
  StaticJsonDocument<256> doc;
  
  doc["state"] = pumpState ? "ON" : "OFF";
  doc["low_sensor"] = lowSensorState;
  doc["high_sensor"] = highSensorState;
  doc["mode"] = overrideMode ? "MANUAL" : "AUTO";
  
  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(MQTT_STATE_TOPIC, buffer);
}

void publishPumpStatus() {
  StaticJsonDocument<128> doc;
  doc["state"] = pumpState ? "ON" : "OFF";
  char buffer[128];
  serializeJson(doc, buffer);
  client.publish(MQTT_STATE_TOPIC, buffer);
}

void publishSensorStatus() {
  StaticJsonDocument<128> doc;
  doc["low_sensor"] = lowSensorState;
  doc["high_sensor"] = highSensorState;
  char buffer[128];
  serializeJson(doc, buffer);
  client.publish(MQTT_STATE_TOPIC, buffer);
}

// =======================
// === EEPROM FUNCTIONS ===
// =======================
void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);

  EEPROM.get(WIFI_SSID_ADDR, wifi_ssid);
  EEPROM.get(WIFI_PASS_ADDR, wifi_password);
  EEPROM.get(MQTT_ADDR, mqtt_server);
  EEPROM.get(MQTT_ADDR + 40, mqtt_user);
  EEPROM.get(MQTT_ADDR + 60, mqtt_password);
  EEPROM.get(MQTT_ADDR + 80, mqtt_port);
  EEPROM.get(OTA_PASS_ADDR, ota_password);

  wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
  wifi_password[sizeof(wifi_password) - 1] = '\0';
  mqtt_server[sizeof(mqtt_server) - 1] = '\0';
  mqtt_user[sizeof(mqtt_user) - 1] = '\0';
  mqtt_password[sizeof(mqtt_password) - 1] = '\0';
  ota_password[sizeof(ota_password) - 1] = '\0';

  if (strlen(mqtt_server) > 0) {
    mqttConfigured = true;
  }

  EEPROM.end();
}

void saveConfig() {
  EEPROM.begin(EEPROM_SIZE);
  
  EEPROM.put(WIFI_SSID_ADDR, wifi_ssid);
  EEPROM.put(WIFI_PASS_ADDR, wifi_password);
  EEPROM.put(MQTT_ADDR, mqtt_server);
  EEPROM.put(MQTT_ADDR + 40, mqtt_user);
  EEPROM.put(MQTT_ADDR + 60, mqtt_password);
  EEPROM.put(MQTT_ADDR + 80, mqtt_port);
  EEPROM.put(OTA_PASS_ADDR, ota_password);

  EEPROM.commit();
  EEPROM.end();
}