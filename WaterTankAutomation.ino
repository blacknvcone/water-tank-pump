#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ESP8266WebServer.h>

// GPIO Pins
#define LOW_SENSOR_PIN 4  // D2
#define HIGH_SENSOR_PIN 5 // D1
#define RELAY_PIN 14      // D5
#define LED_PIN 2         // D4 (Built-in LED, active low)

// EEPROM Addresses
#define EEPROM_SIZE 256
#define WIFI_SSID_ADDR 100
#define WIFI_PASS_ADDR 140
#define MQTT_ADDR 0

WiFiManager wifiManager;
WiFiClient espClient;
PubSubClient client(espClient);
ESP8266WebServer server(80);

char wifi_ssid[40] = "";
char wifi_password[40] = "";
char mqtt_server[40] = "";
char mqtt_user[20] = "";
char mqtt_password[20] = "";
int mqtt_port = 1883;

bool overrideMode = false;
bool overrideState = false;
bool apMode = false;
bool mqttConfigured = false;
bool mqttSkipped = false;

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  if (String(topic) == "waterpump/override") {
    if (message == "ON") {
      overrideMode = true;
      overrideState = true;
      digitalWrite(RELAY_PIN, LOW);
    } else if (message == "OFF") {
      overrideMode = true;
      overrideState = false;
      digitalWrite(RELAY_PIN, HIGH);
    } else if (message == "AUTO") {
      overrideMode = false;
    }
  }
}

void reconnectMQTT() {
 int attempt = 0;
  while (!client.connected() && mqttConfigured && !mqttSkipped && attempt < 5) {
    Serial.print("Attempting MQTT connection... (Attempt ");
    Serial.print(attempt + 1);
    Serial.println("/5)");

    if (client.connect("ESP8266Client", mqtt_user, mqtt_password)) {
      Serial.println("✅ MQTT Connected!");
      client.subscribe("waterpump/override");
      return;  // Exit function if connected
    } else {
      Serial.print("❌ Failed, rc=");
      Serial.print(client.state());
      Serial.println(" -> Retrying in 5 seconds...");
      delay(5000);
      attempt++;
    }
  }

  if (attempt == 5) {
    mqttSkipped = true;
    Serial.println("⚠️ MQTT connection failed after 5 attempts, skipping...");
  }
 
}

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);

  EEPROM.get(WIFI_SSID_ADDR, wifi_ssid);
  EEPROM.get(WIFI_PASS_ADDR, wifi_password);
  EEPROM.get(MQTT_ADDR, mqtt_server);
  EEPROM.get(MQTT_ADDR + 40, mqtt_user);
  EEPROM.get(MQTT_ADDR + 60, mqtt_password);
  EEPROM.get(MQTT_ADDR + 80, mqtt_port);

  wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
  wifi_password[sizeof(wifi_password) - 1] = '\0';
  mqtt_server[sizeof(mqtt_server) - 1] = '\0';
  mqtt_user[sizeof(mqtt_user) - 1] = '\0';
  mqtt_password[sizeof(mqtt_password) - 1] = '\0';

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

  EEPROM.commit();
  EEPROM.end();
}

void setup() {
  Serial.begin(115200);
  pinMode(LOW_SENSOR_PIN, INPUT);
  pinMode(HIGH_SENSOR_PIN, INPUT);
  //pinMode(LOW_SENSOR_PIN, INPUT_PULLUP);
  //pinMode(HIGH_SENSOR_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(LED_PIN, HIGH);
  
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
    Serial.println("AP Mode Started");
  } else {
    digitalWrite(LED_PIN, LOW);
  }
  
  if (mqttConfigured) {
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(mqttCallback);
  }
  
  setupWebServer();
  server.begin();
}

void loop() {
  if (!apMode && mqttConfigured && !client.connected() && !mqttSkipped) {
    reconnectMQTT();
  }
  client.loop();
  server.handleClient();
  handleLED();
  handlePumpLogic();
  Serial.print("Pump State : ");
  Serial.println(String(digitalRead(RELAY_PIN) == LOW ? "ON" : "OFF"));
  delay(1000);
}

void setupWebServer() {
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
                  "<tr><td>Low Sensor</td><td>" + String(digitalRead(LOW_SENSOR_PIN) == LOW ? "Active" : "Inactive") + "</td></tr>"
                  "<tr><td>High Sensor</td><td>" + String(digitalRead(HIGH_SENSOR_PIN) == LOW ? "Active" : "Inactive") + "</td></tr>"
                  "<tr><td>Pump Status</td><td>" + String(digitalRead(RELAY_PIN) == LOW ? "ON" : "OFF") + "</td></tr>"
                  "</table></body></html>";
    server.send(200, "text/html", html);
  });

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
                  "</table><input type='submit' value='Save'></form>"
                   "</body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/save", HTTP_POST, []() {
    String newSSID = server.arg("ssid");
    String newPass = server.arg("wifipass");
    String newMQTT = server.arg("server");
    String newUser = server.arg("user");
    String newPassMQTT = server.arg("pass");
    int newPort = server.arg("port").toInt();

    if (newSSID.length() > 0) newSSID.toCharArray(wifi_ssid, sizeof(wifi_ssid));
    if (newPass.length() > 0) newPass.toCharArray(wifi_password, sizeof(wifi_password));
    if (newMQTT.length() > 0) newMQTT.toCharArray(mqtt_server, sizeof(mqtt_server));
    if (newUser.length() > 0) newUser.toCharArray(mqtt_user, sizeof(mqtt_user));
    if (newPassMQTT.length() > 0) newPassMQTT.toCharArray(mqtt_password, sizeof(mqtt_password));
    if (newPort > 0) mqtt_port = newPort;

    saveConfig();

    server.send(200, "text/html", "<html><body><h3>Settings Saved!</h3><a href='/'>Go Back</a></body></html>");
    
    delay(2000);
    ESP.restart(); // Restart to apply new settings
  });
}

void handlePumpLogic() {
  bool lowSensor = digitalRead(LOW_SENSOR_PIN) == LOW;  // LOW = submerged (water detected)
  bool highSensor = digitalRead(HIGH_SENSOR_PIN) == LOW; // LOW = submerged (water detected)

  if (!overrideMode) {
    if (highSensor) {  
      // 🛑 High sensor detects water → Stop pump immediately
      digitalWrite(RELAY_PIN, HIGH);
      Serial.println("✅ High level detected, turning OFF pump.");
    } 
    else if (!lowSensor && !highSensor) {  
      // 🚨 Both sensors are DRY → Tank is EMPTY → Start pump
      digitalWrite(RELAY_PIN, LOW);
      Serial.println("⚠️ Tank is EMPTY, turning ON pump!");
    }
    else if (!lowSensor) {  
      // 🔄 Low sensor is DRY but high sensor is still dry → Keep pump ON
      digitalWrite(RELAY_PIN, LOW);
      Serial.println("⬆️ Low sensor dry, keeping pump ON until high sensor is wet.");
    }
    else {
      // 🟢 Low sensor is WET but high sensor is still dry → Do nothing (keep previous pump state)
      Serial.println("🟡 Low sensor wet, no change to pump.");
    }
  } else {
    // Manual Override Mode
    digitalWrite(RELAY_PIN, overrideState ? LOW : HIGH);
  }
}


void handleLED() {
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_PIN, LOW);  // LED ON (assuming active low)
  } else {
    static unsigned long lastBlinkTime = 0;
    static bool ledState = false;

    if (millis() - lastBlinkTime >= 500) {  // Blink every 500ms
      lastBlinkTime = millis();
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    }
  }
}
