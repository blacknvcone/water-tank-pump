/**
 * Water Tank Pump Controller v2.0
 *
 * A modular IoT controller for automatic water tank management with:
 * - Automatic pump control based on water level sensors
 * - MQTT integration with Zigbee2MQTT protocol
 * - Web-based configuration interface
 * - OTA firmware updates
 * - Manual override mode via MQTT
 */

#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ==================== CONFIGURATION ====================

// Firmware version
#define FIRMWARE_VERSION "2.0.6"

// EEPROM addresses and size
#define EEPROM_SIZE 256
#define WIFI_SSID_ADDR 100
#define WIFI_PASS_ADDR 140
#define MQTT_ADDR 0
#define OTA_PASS_ADDR 200
#define TIMEZONE_ADDR 220

// GPIO PIN definitions
#define LOW_SENSOR_PIN 4  // D2
#define HIGH_SENSOR_PIN 5 // D1
#define RELAY_PIN 14      // D5
#define LED_PIN 2         // D4 (Built-in LED, active low)

// MQTT topics
#define DEVICE_ID "water_tank_controller"
#define MQTT_STATE_TOPIC "watertank/" DEVICE_ID
#define MQTT_COMMAND_TOPIC "watertank/" DEVICE_ID "/set"
#define MQTT_AVAILABILITY_TOPIC "watertank/" DEVICE_ID "/availability"

// Home Assistant MQTT Discovery
#define HA_DISCOVERY_PREFIX "homeassistant"

// ==================== SETTINGS CLASS ====================

class Settings
{
public:
  Settings();

  void begin();
  void load();
  void save();

  // WiFi settings
  char wifi_ssid[40];
  char wifi_password[40];

  // MQTT settings
  char mqtt_server[40];
  char mqtt_user[20];
  char mqtt_password[20];
  int mqtt_port;

  // OTA settings
  char ota_password[20];

  // Timezone settings (offset in hours from UTC, e.g., 7 for GMT+7)
  int timezone_offset;

  // Check if MQTT is configured
  bool isMqttConfigured() const;

private:
  bool _mqttConfigured;
};

Settings::Settings() : mqtt_port(1883),
                       timezone_offset(7),
                       _mqttConfigured(false)
{

  // Initialize strings to empty
  wifi_ssid[0] = '\0';
  wifi_password[0] = '\0';
  mqtt_server[0] = '\0';
  mqtt_user[0] = '\0';
  mqtt_password[0] = '\0';

  // Default OTA password
  strncpy(ota_password, "astalavista", sizeof(ota_password) - 1);
  ota_password[sizeof(ota_password) - 1] = '\0';
}

void Settings::begin()
{
  load();
}

void Settings::load()
{
  EEPROM.begin(EEPROM_SIZE);

  EEPROM.get(WIFI_SSID_ADDR, wifi_ssid);
  EEPROM.get(WIFI_PASS_ADDR, wifi_password);
  EEPROM.get(MQTT_ADDR, mqtt_server);
  EEPROM.get(MQTT_ADDR + 40, mqtt_user);
  EEPROM.get(MQTT_ADDR + 60, mqtt_password);
  EEPROM.get(MQTT_ADDR + 80, mqtt_port);

  // Load OTA password from EEPROM
  char temp_ota_password[20];
  EEPROM.get(OTA_PASS_ADDR, temp_ota_password);
  temp_ota_password[sizeof(temp_ota_password) - 1] = '\0';

  // Only use EEPROM password if it contains printable characters
  // Otherwise keep the default password
  if (temp_ota_password[0] != '\0' && temp_ota_password[0] >= 32 && temp_ota_password[0] <= 126)
  {
    memcpy(ota_password, temp_ota_password, sizeof(ota_password));
  }

  // Load timezone offset
  int temp_timezone;
  EEPROM.get(TIMEZONE_ADDR, temp_timezone);

  // Check if timezone has been configured (value between -12 and +14, but not likely garbage)
  // Common garbage values from uninitialized EEPROM: 0, -1, 255, 65535
  bool timezone_valid = (temp_timezone >= -12 && temp_timezone <= 14);
  bool timezone_configured = timezone_valid && (temp_timezone != 0) && (temp_timezone != -1);

  if (timezone_configured)
  {
    timezone_offset = temp_timezone;
    Serial.print("Loaded timezone from EEPROM: GMT");
    if (temp_timezone >= 0)
      Serial.print("+");
    Serial.println(temp_timezone);
  }
  else
  {
    // Keep default GMT+7 and write it to EEPROM for next boot
    Serial.println("Using default timezone: GMT+7");
    EEPROM.put(TIMEZONE_ADDR, timezone_offset);
    EEPROM.commit();
  }

  // Ensure null termination
  wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
  wifi_password[sizeof(wifi_password) - 1] = '\0';
  mqtt_server[sizeof(mqtt_server) - 1] = '\0';
  mqtt_user[sizeof(mqtt_user) - 1] = '\0';
  mqtt_password[sizeof(mqtt_password) - 1] = '\0';
  ota_password[sizeof(ota_password) - 1] = '\0';

  _mqttConfigured = (strlen(mqtt_server) > 0);

  EEPROM.end();
}

void Settings::save()
{
  EEPROM.begin(EEPROM_SIZE);

  EEPROM.put(WIFI_SSID_ADDR, wifi_ssid);
  EEPROM.put(WIFI_PASS_ADDR, wifi_password);
  EEPROM.put(MQTT_ADDR, mqtt_server);
  EEPROM.put(MQTT_ADDR + 40, mqtt_user);
  EEPROM.put(MQTT_ADDR + 60, mqtt_password);
  EEPROM.put(MQTT_ADDR + 80, mqtt_port);
  EEPROM.put(OTA_PASS_ADDR, ota_password);
  EEPROM.put(TIMEZONE_ADDR, timezone_offset);

  EEPROM.commit();
  EEPROM.end();

  _mqttConfigured = (strlen(mqtt_server) > 0);
}

bool Settings::isMqttConfigured() const
{
  return _mqttConfigured;
}

// ==================== FORWARD DECLARATIONS ====================

// Declare global instances (defined at the end of file)
extern Settings settings;
extern class SystemManager systemManager;
extern class WaterLevelSensor waterLevel;
extern class PumpController pumpController;
extern class MqttHandler mqttClient;
extern class WebServerHandler webServer;

// ==================== SYSTEM MANAGER CLASS ====================

class SystemManager
{
public:
  SystemManager();

  void begin();
  void loop();

  // WiFi management
  bool isWiFiConnected() const;
  bool isAPMode() const;
  String getIPAddress() const;

  // Time synchronization
  bool isTimeSynced() const;
  time_t getCurrentTime() const;
  void startTimeSync();

  // LED management
  void updateLED(bool pumpState, bool overrideMode);

private:
  bool _apMode;
  bool _timeSyncStarted;
  bool _timeSynced;
  unsigned long _ledBlinkTimer;
  bool _ledState;

  void checkTimeSync();
};

SystemManager::SystemManager() : _apMode(false),
                                 _timeSyncStarted(false),
                                 _timeSynced(false),
                                 _ledBlinkTimer(0),
                                 _ledState(false)
{
}

void SystemManager::begin()
{
  Serial.begin(115200);
  Serial.println("\n\nWater Tank Controller v" FIRMWARE_VERSION);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // LED OFF (active low)

  // Load settings
  settings.begin();

  // Try to connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(settings.wifi_ssid, settings.wifi_password);

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000)
  {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("\nFailed to connect. Starting AP mode...");
    WiFi.softAP("WaterTank-Setup");
    _apMode = true;
    Serial.print("AP Started. IP: ");
    Serial.println(WiFi.softAPIP());
  }
  else
  {
    Serial.println("\nConnected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    digitalWrite(LED_PIN, LOW); // LED ON when connected
    startTimeSync();
  }
}

void SystemManager::loop()
{
  if (!_apMode && WiFi.status() == WL_CONNECTED)
  {
    if (!_timeSyncStarted)
    {
      startTimeSync();
    }
    if (!_timeSynced)
    {
      checkTimeSync();
    }
  }
}

bool SystemManager::isWiFiConnected() const
{
  return WiFi.status() == WL_CONNECTED;
}

bool SystemManager::isAPMode() const
{
  return _apMode;
}

String SystemManager::getIPAddress() const
{
  if (_apMode)
  {
    return WiFi.softAPIP().toString();
  }
  return WiFi.localIP().toString();
}

bool SystemManager::isTimeSynced() const
{
  return _timeSynced;
}

time_t SystemManager::getCurrentTime() const
{
  return time(nullptr);
}

void SystemManager::startTimeSync()
{
  if (_timeSyncStarted)
    return;

  Serial.println("Starting time sync with NTP...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  _timeSyncStarted = true;
}

void SystemManager::checkTimeSync()
{
  time_t now = time(nullptr);
  if (now > 1000000000)
  { // Valid timestamp (after 2001)
    _timeSynced = true;
    Serial.print("Time synced: ");
    Serial.println(ctime(&now));
  }
}

void SystemManager::updateLED(bool pumpState, bool overrideMode)
{
  // Priority 1: When pump is ON, blink very fast (overrides all other states)
  if (pumpState)
  {
    // Blink very fast when pump is running (10 blinks per second)
    if (millis() - _ledBlinkTimer > 100)
    {
      _ledState = !_ledState;
      digitalWrite(LED_PIN, _ledState ? LOW : HIGH);
      _ledBlinkTimer = millis();
    }
  }
  // Priority 2: When pump is OFF, show connection status
  else if (!isWiFiConnected())
  {
    // Blink very slow when WiFi disconnected (1 blink per second)
    if (millis() - _ledBlinkTimer > 1000)
    {
      _ledState = !_ledState;
      digitalWrite(LED_PIN, _ledState ? LOW : HIGH);
      _ledBlinkTimer = millis();
    }
  }
  else
  {
    // Solid on when WiFi connected and pump is off
    digitalWrite(LED_PIN, LOW);
  }
}

// ==================== WATER LEVEL SENSOR CLASS ====================

class WaterLevelSensor
{
public:
  WaterLevelSensor();

  void begin();
  void update();

  // Get sensor states
  bool isLowWaterDetected() const;
  bool isHighWaterDetected() const;

  // Check if sensor states have changed
  bool hasLowSensorChanged() const;
  bool hasHighSensorChanged() const;

  // Reset change flags after handling
  void resetChangeFlags();

private:
  bool _lowSensorState;
  bool _highSensorState;
  bool _lastLowSensorState;
  bool _lastHighSensorState;

  // Helper function to read sensors with debounce
  bool readSensor(int pin);
};

WaterLevelSensor::WaterLevelSensor() : _lowSensorState(false),
                                       _highSensorState(false),
                                       _lastLowSensorState(false),
                                       _lastHighSensorState(false)
{
}

void WaterLevelSensor::begin()
{
  pinMode(LOW_SENSOR_PIN, INPUT);
  pinMode(HIGH_SENSOR_PIN, INPUT);

  // Initial read
  update();

  // Initialize last states to match current states
  _lastLowSensorState = _lowSensorState;
  _lastHighSensorState = _highSensorState;
}

void WaterLevelSensor::update()
{
  // Store previous states
  _lastLowSensorState = _lowSensorState;
  _lastHighSensorState = _highSensorState;

  // Read current states
  _lowSensorState = readSensor(LOW_SENSOR_PIN);
  _highSensorState = readSensor(HIGH_SENSOR_PIN);
}

bool WaterLevelSensor::isLowWaterDetected() const
{
  return _lowSensorState;
}

bool WaterLevelSensor::isHighWaterDetected() const
{
  return _highSensorState;
}

bool WaterLevelSensor::hasLowSensorChanged() const
{
  return _lowSensorState != _lastLowSensorState;
}

bool WaterLevelSensor::hasHighSensorChanged() const
{
  return _highSensorState != _lastHighSensorState;
}

void WaterLevelSensor::resetChangeFlags()
{
  _lastLowSensorState = _lowSensorState;
  _lastHighSensorState = _highSensorState;
}

bool WaterLevelSensor::readSensor(int pin)
{
  // Read multiple times for debounce
  int activeCount = 0;
  for (int i = 0; i < 5; i++)
  {
    if (digitalRead(pin) == HIGH)
    {
      activeCount++;
    }
    delay(2); // Reduced delay to prevent watchdog issues
    yield();  // Allow ESP8266 to handle background tasks
  }
  return activeCount >= 3; // Majority vote
}

// ==================== PUMP CONTROLLER CLASS ====================

class PumpController
{
public:
  PumpController();

  void begin();
  void loop();

  // Control methods
  void setOverrideMode(bool enabled, bool state);
  bool isOverrideMode() const;
  bool getOverrideState() const;
  bool getPumpState() const;

  // State change detection
  bool hasPumpStateChanged();

  // Timing information
  unsigned long getLastOnTime() const;
  unsigned long getLastOffTime() const;
  time_t getLastOnEpoch() const;
  time_t getLastOffEpoch() const;
  unsigned long getLastPumpDuration() const; // Last ON-to-OFF duration in milliseconds

  // Manual control for updating timestamps
  void updateTimestamps(time_t currentTime);

private:
  bool _overrideMode;
  bool _overrideState;
  bool _pumpState;
  bool _lastPumpState;

  // Timestamps
  unsigned long _pumpLastOnAt;     // milliseconds since boot
  unsigned long _pumpLastOffAt;    // milliseconds since boot
  time_t _pumpLastOnEpoch;         // seconds since epoch (UTC)
  time_t _pumpLastOffEpoch;        // seconds since epoch (UTC)
  unsigned long _lastPumpDuration; // Last pump ON-to-OFF duration in milliseconds

  void setPumpState(bool state, time_t currentTime);
  void handleAutomaticControl(time_t currentTime);
};

PumpController::PumpController() : _overrideMode(false),
                                   _overrideState(false),
                                   _pumpState(false),
                                   _lastPumpState(false),
                                   _pumpLastOnAt(0),
                                   _pumpLastOffAt(0),
                                   _pumpLastOnEpoch(0),
                                   _pumpLastOffEpoch(0),
                                   _lastPumpDuration(0)
{
}

void PumpController::begin()
{
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Initially OFF

  Serial.println("Pump controller initialized");
}

void PumpController::loop()
{
  time_t currentTime = time(nullptr);

  if (_overrideMode)
  {
    // Manual override mode - use override state
    if (_pumpState != _overrideState)
    {
      setPumpState(_overrideState, currentTime);
    }
  }
  else
  {
    // Automatic mode based on water level sensors
    handleAutomaticControl(currentTime);
  }
}

void PumpController::setOverrideMode(bool enabled, bool state)
{
  _overrideMode = enabled;
  _overrideState = state;

  Serial.print("Override mode ");
  Serial.print(enabled ? "ENABLED" : "DISABLED");
  if (enabled)
  {
    Serial.print(" - State: ");
    Serial.println(state ? "ON" : "OFF");
  }
  else
  {
    Serial.println();
  }
}

bool PumpController::isOverrideMode() const
{
  return _overrideMode;
}

bool PumpController::getOverrideState() const
{
  return _overrideState;
}

bool PumpController::getPumpState() const
{
  return _pumpState;
}

bool PumpController::hasPumpStateChanged()
{
  return _pumpState != _lastPumpState;
}

unsigned long PumpController::getLastOnTime() const
{
  return _pumpLastOnAt;
}

unsigned long PumpController::getLastOffTime() const
{
  return _pumpLastOffAt;
}

time_t PumpController::getLastOnEpoch() const
{
  return _pumpLastOnEpoch;
}

time_t PumpController::getLastOffEpoch() const
{
  return _pumpLastOffEpoch;
}

unsigned long PumpController::getLastPumpDuration() const
{
  return _lastPumpDuration;
}

void PumpController::updateTimestamps(time_t currentTime)
{
  // Update epoch timestamps if time is synced and we have boot timestamps
  if (currentTime > 1000000000)
  {
    unsigned long currentMillis = millis();

    if (_pumpLastOnAt > 0)
    {
      unsigned long elapsedSinceOn = currentMillis - _pumpLastOnAt;
      _pumpLastOnEpoch = currentTime - (elapsedSinceOn / 1000);
    }

    if (_pumpLastOffAt > 0)
    {
      unsigned long elapsedSinceOff = currentMillis - _pumpLastOffAt;
      _pumpLastOffEpoch = currentTime - (elapsedSinceOff / 1000);
    }
  }
}

void PumpController::setPumpState(bool state, time_t currentTime)
{
  if (_pumpState != state)
  {
    _lastPumpState = _pumpState; // Save old state before changing
    _pumpState = state;
    digitalWrite(RELAY_PIN, state ? HIGH : LOW);

    Serial.print("Pump ");
    Serial.println(state ? "ON" : "OFF");

    // Update timestamps
    if (state)
    {
      _pumpLastOnAt = millis();
      if (currentTime > 1000000000)
      {
        _pumpLastOnEpoch = currentTime;
      }
    }
    else
    {
      _pumpLastOffAt = millis();
      if (currentTime > 1000000000)
      {
        _pumpLastOffEpoch = currentTime;
      }

      // Calculate pump duration (ON to OFF) in milliseconds
      if (_pumpLastOnAt > 0 && _pumpLastOffAt > _pumpLastOnAt)
      {
        _lastPumpDuration = _pumpLastOffAt - _pumpLastOnAt;
        Serial.print("Pump ran for: ");
        Serial.print(_lastPumpDuration / 1000);
        Serial.println(" seconds");
      }
    }
  }
}

void PumpController::handleAutomaticControl(time_t currentTime)
{
  // Update water level sensor readings
  waterLevel.update();

  bool lowWater = waterLevel.isLowWaterDetected();
  bool highWater = waterLevel.isHighWaterDetected();

  // Pump control logic with proper hysteresis:
  // - Turn ON when low sensor is NOT triggered (water below low sensor)
  // - Turn OFF when high sensor IS triggered (water reaches high sensor)
  // - Maintain current state when water is between sensors (hysteresis zone)
  //
  // Expected sensor behavior: HIGH = water present, LOW = water absent
  // So we invert the logic: !lowWater means water is below low sensor

  if (!lowWater && !highWater)
  {
    // Water is below low sensor, turn pump ON
    setPumpState(true, currentTime);
  }
  else if (highWater)
  {
    // Water reached high sensor, turn pump OFF
    setPumpState(false, currentTime);
  }
  // If only low sensor is triggered (water between sensors), maintain current state (hysteresis)
}

// ==================== MQTT HANDLER CLASS ====================

class MqttHandler
{
public:
  MqttHandler();

  void begin();
  void loop();

  // Connection management
  bool isConnected() const;
  bool connect();

  // Publishing methods
  void publishState();
  void publishPumpStatus(bool pumpState, unsigned long lastOnTime, unsigned long lastOffTime,
                         time_t lastOnEpoch, time_t lastOffEpoch, unsigned long lastDuration);
  void publishSensorStatus();
  void publishDiscovery(); // Home Assistant MQTT Discovery

  // Command callback handling
  typedef std::function<void(bool overrideMode, bool overrideState)> CommandCallback;
  void setCommandCallback(CommandCallback callback);

private:
  WiFiClient _wifiClient;
  PubSubClient _client;
  CommandCallback _commandCallback;
  unsigned long _lastStatusUpdate;
  const long _statusUpdateInterval = 5000; // 5 seconds

  // Format time as ISO8601
  bool formatISO8601(time_t t, char *out, size_t len);

  // Static callback wrapper for PubSubClient
  static void mqttCallbackWrapper(char *topic, byte *payload, unsigned int length);
  static MqttHandler *_instance;

  // Actual callback implementation
  void mqttCallback(char *topic, byte *payload, unsigned int length);
};

MqttHandler *MqttHandler::_instance = nullptr;

MqttHandler::MqttHandler() : _client(_wifiClient),
                             _lastStatusUpdate(0)
{
  _instance = this;
}

void MqttHandler::begin()
{
  if (!settings.isMqttConfigured())
  {
    Serial.println("MQTT not configured, skipping...");
    return;
  }

  _client.setServer(settings.mqtt_server, settings.mqtt_port);
  _client.setCallback(mqttCallbackWrapper);

  Serial.print("MQTT configured for: ");
  Serial.print(settings.mqtt_server);
  Serial.print(":");
  Serial.println(settings.mqtt_port);
}

void MqttHandler::loop()
{
  if (!settings.isMqttConfigured())
  {
    return;
  }

  // Add yield to prevent watchdog resets
  yield();

  if (!_client.connected())
  {
    // Throttle reconnection attempts to prevent blocking
    static unsigned long lastConnectAttempt = 0;
    if (millis() - lastConnectAttempt > 5000)
    { // Try every 5 seconds
      connect();
      lastConnectAttempt = millis();
    }
  }

  if (_client.connected())
  {
    _client.loop();

    // Periodically publish state updates
    if (millis() - _lastStatusUpdate > _statusUpdateInterval)
    {
      publishState();
      _lastStatusUpdate = millis();
    }
  }
}

bool MqttHandler::isConnected() const
{
  return const_cast<PubSubClient &>(_client).connected();
}

bool MqttHandler::connect()
{
  if (_client.connected())
  {
    return true;
  }

  Serial.print("Connecting to MQTT...");

  String clientId = String(DEVICE_ID) + "_" + String(ESP.getChipId(), HEX);

  bool connected = false;
  if (strlen(settings.mqtt_user) > 0)
  {
    connected = _client.connect(clientId.c_str(), settings.mqtt_user, settings.mqtt_password);
  }
  else
  {
    connected = _client.connect(clientId.c_str());
  }

  if (connected)
  {
    Serial.println("Connected!");

    // Subscribe to command topic
    _client.subscribe(MQTT_COMMAND_TOPIC);
    Serial.print("Subscribed to: ");
    Serial.println(MQTT_COMMAND_TOPIC);

    // Publish Home Assistant MQTT Discovery
    publishDiscovery();

    // Publish initial state
    publishState();

    // Publish initial pump status
    publishPumpStatus(
        pumpController.getPumpState(),
        pumpController.getLastOnTime(),
        pumpController.getLastOffTime(),
        pumpController.getLastOnEpoch(),
        pumpController.getLastOffEpoch(),
        pumpController.getLastPumpDuration());

    return true;
  }
  else
  {
    Serial.print("Failed, rc=");
    Serial.println(_client.state());
    return false;
  }
}

void MqttHandler::publishState()
{
  if (!_client.connected())
  {
    return;
  }

  // Create JSON document
  StaticJsonDocument<512> doc;

  // Add firmware version
  doc["firmware_version"] = FIRMWARE_VERSION;

  // Add sensor states
  doc["contact"] = waterLevel.isLowWaterDetected();
  doc["water_leak"] = waterLevel.isHighWaterDetected();

  // Add control mode
  doc["control_mode"] = pumpController.isOverrideMode() ? "manual" : "automatic";

  // Add override state (for mode select dropdown)
  if (pumpController.isOverrideMode())
  {
    doc["override_state"] = pumpController.getOverrideState();
  }

  // Add link quality (WiFi RSSI mapped to 0-255)
  long rssi = WiFi.RSSI();
  int linkQuality = map(constrain(rssi, -100, -50), -100, -50, 0, 255);
  doc["linkquality"] = linkQuality;

  // Serialize to string
  char buffer[512];
  serializeJson(doc, buffer);

  // Publish
  _client.publish(MQTT_STATE_TOPIC, buffer, true);

  Serial.print("Published state: ");
  Serial.println(buffer);
}

void MqttHandler::publishPumpStatus(bool pumpState, unsigned long lastOnTime, unsigned long lastOffTime,
                                    time_t lastOnEpoch, time_t lastOffEpoch, unsigned long lastDuration)
{
  if (!_client.connected())
  {
    return;
  }

  StaticJsonDocument<512> doc;

  doc["state"] = pumpState ? "ON" : "OFF";

  // Add timestamps if available
  if (lastOnEpoch > 1000000000)
  {
    char isoTime[30];
    if (formatISO8601(lastOnEpoch, isoTime, sizeof(isoTime)))
    {
      doc["last_on"] = isoTime;
    }
  }

  if (lastOffEpoch > 1000000000)
  {
    char isoTime[30];
    if (formatISO8601(lastOffEpoch, isoTime, sizeof(isoTime)))
    {
      doc["last_off"] = isoTime;
    }
  }

  // Add runtime info
  doc["runtime_last_on"] = lastOnTime;
  doc["runtime_last_off"] = lastOffTime;

  // Add last pump duration (in seconds for readability)
  if (lastDuration > 0)
  {
    doc["last_duration_seconds"] = lastDuration / 1000;
    doc["last_duration_ms"] = lastDuration;
  }

  char buffer[512];
  serializeJson(doc, buffer);

  String pumpTopic = String(MQTT_STATE_TOPIC) + "/pump";
  _client.publish(pumpTopic.c_str(), buffer, true);

  Serial.print("Published pump status: ");
  Serial.println(buffer);
}

void MqttHandler::publishSensorStatus()
{
  // This is included in the main publishState() method
  publishState();
}

void MqttHandler::publishDiscovery()
{
  if (!_client.connected())
  {
    return;
  }

  Serial.println("Publishing Home Assistant MQTT Discovery...");

  String deviceId = DEVICE_ID;
  String uniqueIdBase = String("wtc_") + String(ESP.getChipId(), HEX);

  // Device information (shared across all entities)
  String deviceInfo = "\"device\":{";
  deviceInfo += "\"identifiers\":[\"" + uniqueIdBase + "\"],";
  deviceInfo += "\"name\":\"Water Tank Controller\",";
  deviceInfo += "\"model\":\"ESP8266 Water Tank Pump Controller\",";
  deviceInfo += "\"manufacturer\":\"DIY\",";
  deviceInfo += "\"sw_version\":\"" + String(FIRMWARE_VERSION) + "\"";
  deviceInfo += "}";

  // 1. Low Water Sensor (Binary Sensor)
  {
    String topic = String(HA_DISCOVERY_PREFIX) + "/binary_sensor/" + deviceId + "_low_water/config";
    String payload = "{";
    payload += "\"name\":\"Water Tank Low Level\",";
    payload += "\"unique_id\":\"" + uniqueIdBase + "_low_water\",";
    payload += "\"state_topic\":\"" + String(MQTT_STATE_TOPIC) + "\",";
    payload += "\"value_template\":\"{{ value_json.contact }}\",";
    payload += "\"payload_on\":true,";
    payload += "\"payload_off\":false,";
    payload += "\"device_class\":\"moisture\",";
    payload += deviceInfo;
    payload += "}";
    _client.publish(topic.c_str(), payload.c_str(), true);
    yield();
  }

  // 2. High Water Sensor (Binary Sensor)
  {
    String topic = String(HA_DISCOVERY_PREFIX) + "/binary_sensor/" + deviceId + "_high_water/config";
    String payload = "{";
    payload += "\"name\":\"Water Tank High Level\",";
    payload += "\"unique_id\":\"" + uniqueIdBase + "_high_water\",";
    payload += "\"state_topic\":\"" + String(MQTT_STATE_TOPIC) + "\",";
    payload += "\"value_template\":\"{{ value_json.water_leak }}\",";
    payload += "\"payload_on\":true,";
    payload += "\"payload_off\":false,";
    payload += "\"device_class\":\"moisture\",";
    payload += deviceInfo;
    payload += "}";
    _client.publish(topic.c_str(), payload.c_str(), true);
    yield();
  }

  // 3. Pump State (Binary Sensor)
  {
    String topic = String(HA_DISCOVERY_PREFIX) + "/binary_sensor/" + deviceId + "_pump/config";
    String payload = "{";
    payload += "\"name\":\"Water Tank Pump State\",";
    payload += "\"unique_id\":\"" + uniqueIdBase + "_pump_state\",";
    payload += "\"state_topic\":\"" + String(MQTT_STATE_TOPIC) + "/pump\",";
    payload += "\"value_template\":\"{{ value_json.state }}\",";
    payload += "\"payload_on\":\"ON\",";
    payload += "\"payload_off\":\"OFF\",";
    payload += "\"device_class\":\"running\",";
    payload += "\"icon\":\"mdi:water-pump\",";
    payload += deviceInfo;
    payload += "}";
    _client.publish(topic.c_str(), payload.c_str(), true);
    yield();
  }

  // 4. Pump Control Switch
  {
    String topic = String(HA_DISCOVERY_PREFIX) + "/switch/" + deviceId + "_control/config";
    String payload = "{";
    payload += "\"name\":\"Water Tank Pump Control\",";
    payload += "\"unique_id\":\"" + uniqueIdBase + "_pump_control\",";
    payload += "\"state_topic\":\"" + String(MQTT_STATE_TOPIC) + "/pump\",";
    payload += "\"value_template\":\"{{ value_json.state }}\",";
    payload += "\"command_topic\":\"" + String(MQTT_COMMAND_TOPIC) + "\",";
    payload += "\"payload_on\":\"{\\\"override\\\":true,\\\"state\\\":\\\"ON\\\"}\",";
    payload += "\"payload_off\":\"{\\\"override\\\":true,\\\"state\\\":\\\"OFF\\\"}\",";
    payload += "\"state_on\":\"ON\",";
    payload += "\"state_off\":\"OFF\",";
    payload += "\"icon\":\"mdi:water-pump\",";
    payload += deviceInfo;
    payload += "}";
    _client.publish(topic.c_str(), payload.c_str(), true);
    yield();
  }

  // 5. WiFi Signal Strength (Sensor)
  {
    String topic = String(HA_DISCOVERY_PREFIX) + "/sensor/" + deviceId + "_rssi/config";
    String payload = "{";
    payload += "\"name\":\"Water Tank WiFi Signal\",";
    payload += "\"unique_id\":\"" + uniqueIdBase + "_rssi\",";
    payload += "\"state_topic\":\"" + String(MQTT_STATE_TOPIC) + "\",";
    payload += "\"value_template\":\"{{ value_json.linkquality }}\",";
    payload += "\"unit_of_measurement\":\"lqi\",";
    payload += "\"icon\":\"mdi:wifi\",";
    payload += "\"state_class\":\"measurement\",";
    payload += deviceInfo;
    payload += "}";
    _client.publish(topic.c_str(), payload.c_str(), true);
    yield();
  }

  // 6. Last Pump Duration (Sensor)
  {
    String topic = String(HA_DISCOVERY_PREFIX) + "/sensor/" + deviceId + "_duration/config";
    String payload = "{";
    payload += "\"name\":\"Water Tank Last Pump Duration\",";
    payload += "\"unique_id\":\"" + uniqueIdBase + "_duration\",";
    payload += "\"state_topic\":\"" + String(MQTT_STATE_TOPIC) + "/pump\",";
    payload += "\"value_template\":\"{{ value_json.last_duration_seconds }}\",";
    payload += "\"unit_of_measurement\":\"s\",";
    payload += "\"icon\":\"mdi:timer\",";
    payload += "\"state_class\":\"measurement\",";
    payload += deviceInfo;
    payload += "}";
    _client.publish(topic.c_str(), payload.c_str(), true);
    yield();
  }

  // 7. Firmware Version (Sensor)
  {
    String topic = String(HA_DISCOVERY_PREFIX) + "/sensor/" + deviceId + "_firmware/config";
    String payload = "{";
    payload += "\"name\":\"Water Tank Firmware Version\",";
    payload += "\"unique_id\":\"" + uniqueIdBase + "_firmware\",";
    payload += "\"state_topic\":\"" + String(MQTT_STATE_TOPIC) + "\",";
    payload += "\"value_template\":\"{{ value_json.firmware_version }}\",";
    payload += "\"icon\":\"mdi:chip\",";
    payload += deviceInfo;
    payload += "}";
    _client.publish(topic.c_str(), payload.c_str(), true);
    yield();
  }

  // 8. Last ON Timestamp (Sensor)
  {
    String topic = String(HA_DISCOVERY_PREFIX) + "/sensor/" + deviceId + "_last_on/config";
    String payload = "{";
    payload += "\"name\":\"Water Tank Pump Last ON\",";
    payload += "\"unique_id\":\"" + uniqueIdBase + "_last_on\",";
    payload += "\"state_topic\":\"" + String(MQTT_STATE_TOPIC) + "/pump\",";
    payload += "\"value_template\":\"{{ value_json.last_on }}\",";
    payload += "\"device_class\":\"timestamp\",";
    payload += "\"icon\":\"mdi:clock-start\",";
    payload += deviceInfo;
    payload += "}";
    _client.publish(topic.c_str(), payload.c_str(), true);
    yield();
  }

  // 9. Last OFF Timestamp (Sensor)
  {
    String topic = String(HA_DISCOVERY_PREFIX) + "/sensor/" + deviceId + "_last_off/config";
    String payload = "{";
    payload += "\"name\":\"Water Tank Pump Last OFF\",";
    payload += "\"unique_id\":\"" + uniqueIdBase + "_last_off\",";
    payload += "\"state_topic\":\"" + String(MQTT_STATE_TOPIC) + "/pump\",";
    payload += "\"value_template\":\"{{ value_json.last_off }}\",";
    payload += "\"device_class\":\"timestamp\",";
    payload += "\"icon\":\"mdi:clock-end\",";
    payload += deviceInfo;
    payload += "}";
    _client.publish(topic.c_str(), payload.c_str(), true);
    yield();
  }

  // 10. Control Mode (Sensor)
  {
    String topic = String(HA_DISCOVERY_PREFIX) + "/sensor/" + deviceId + "_control_mode/config";
    String payload = "{";
    payload += "\"name\":\"Water Tank Control Mode\",";
    payload += "\"unique_id\":\"" + uniqueIdBase + "_control_mode\",";
    payload += "\"state_topic\":\"" + String(MQTT_STATE_TOPIC) + "\",";
    payload += "\"value_template\":\"{{ value_json.control_mode }}\",";
    payload += "\"icon\":\"mdi:cog\",";
    payload += deviceInfo;
    payload += "}";
    _client.publish(topic.c_str(), payload.c_str(), true);
    yield();
  }

  // 11. Control Mode Select (to switch between Automatic/Manual ON/Manual OFF)
  {
    String topic = String(HA_DISCOVERY_PREFIX) + "/select/" + deviceId + "_mode_select/config";
    String payload = "{";
    payload += "\"name\":\"Water Tank Mode Select\",";
    payload += "\"unique_id\":\"" + uniqueIdBase + "_mode_select\",";
    payload += "\"state_topic\":\"" + String(MQTT_STATE_TOPIC) + "\",";
    payload += "\"value_template\":\"{% if value_json.control_mode == 'automatic' %}Automatic{% elif value_json.override_state == true %}Manual ON{% else %}Manual OFF{% endif %}\",";
    payload += "\"command_topic\":\"" + String(MQTT_COMMAND_TOPIC) + "\",";
    payload += "\"command_template\":\"{% if value == 'Automatic' %}{\\\"override\\\":false}{% elif value == 'Manual ON' %}{\\\"override\\\":true,\\\"state\\\":\\\"ON\\\"}{% else %}{\\\"override\\\":true,\\\"state\\\":\\\"OFF\\\"}{% endif %}\",";
    payload += "\"options\":[\"Automatic\",\"Manual ON\",\"Manual OFF\"],";
    payload += "\"icon\":\"mdi:dip-switch\",";
    payload += deviceInfo;
    payload += "}";
    _client.publish(topic.c_str(), payload.c_str(), true);
    yield();
  }

  Serial.println("Home Assistant MQTT Discovery published successfully!");
}

void MqttHandler::setCommandCallback(CommandCallback callback)
{
  _commandCallback = callback;
}

bool MqttHandler::formatISO8601(time_t t, char *out, size_t len)
{
  struct tm *timeinfo = gmtime(&t);
  if (!timeinfo)
  {
    return false;
  }

  // Format: YYYY-MM-DDTHH:MM:SSZ
  int written = snprintf(out, len, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                         timeinfo->tm_year + 1900,
                         timeinfo->tm_mon + 1,
                         timeinfo->tm_mday,
                         timeinfo->tm_hour,
                         timeinfo->tm_min,
                         timeinfo->tm_sec);

  return written > 0 && written < (int)len;
}

void MqttHandler::mqttCallbackWrapper(char *topic, byte *payload, unsigned int length)
{
  if (_instance)
  {
    _instance->mqttCallback(topic, payload, length);
  }
}

void MqttHandler::mqttCallback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("MQTT message received on topic: ");
  Serial.println(topic);

  // Parse JSON payload
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error)
  {
    Serial.print("JSON parse failed: ");
    Serial.println(error.c_str());
    return;
  }

  // Check if this is a command for our device
  if (strcmp(topic, MQTT_COMMAND_TOPIC) == 0)
  {
    // Handle override mode commands
    if (doc.containsKey("override"))
    {
      bool overrideMode = doc["override"].as<bool>();
      bool overrideState = false;

      if (doc.containsKey("state"))
      {
        String state = doc["state"].as<String>();
        overrideState = (state == "ON");
      }

      // Call the callback if set
      if (_commandCallback)
      {
        _commandCallback(overrideMode, overrideState);
      }

      Serial.print("Override command: mode=");
      Serial.print(overrideMode);
      Serial.print(", state=");
      Serial.println(overrideState ? "ON" : "OFF");
    }
  }
}

// ==================== WEB SERVER HANDLER CLASS ====================

class WebServerHandler
{
public:
  WebServerHandler();

  void begin();
  void loop();

private:
  ESP8266WebServer _server;
  ESP8266HTTPUpdateServer _httpUpdater;

  // Route handlers
  void handleRoot();
  void handleSetup();
  void handleSave();
  void handlePumpControl();
  void handleNotFound();

  // Helper methods
  String buildStatusPage();
  String buildSetupPage();
  String formatDuration(unsigned long milliseconds);
  String formatPumpDuration(unsigned long milliseconds);
  String formatDateTime(time_t epoch);
};

WebServerHandler::WebServerHandler() : _server(80)
{
}

void WebServerHandler::begin()
{
  // Setup routes
  _server.on("/", [this]()
             { handleRoot(); });
  _server.on("/setup", [this]()
             { handleSetup(); });
  _server.on("/save", HTTP_POST, [this]()
             { handleSave(); });
  _server.on("/pump", HTTP_POST, [this]()
             { handlePumpControl(); });
  _server.onNotFound([this]()
                     { handleNotFound(); });

  // Setup OTA update handler
  Serial.print("Setting up OTA with password: ");
  Serial.println(settings.ota_password);
  _httpUpdater.setup(&_server, "/update", "admin", settings.ota_password);

  _server.begin();
  Serial.println("Web server started on port 80");
  Serial.print("Access at: http://");
  Serial.println(systemManager.getIPAddress());
  Serial.println("OTA Update available at: http://" + systemManager.getIPAddress() + "/update");
}

void WebServerHandler::loop()
{
  _server.handleClient();
}

void WebServerHandler::handleRoot()
{
  String html = buildStatusPage();
  _server.send(200, "text/html", html);
}

void WebServerHandler::handleSetup()
{
  String html = buildSetupPage();
  _server.send(200, "text/html", html);
}

void WebServerHandler::handleSave()
{
  // Save WiFi settings
  if (_server.hasArg("wifi_ssid"))
  {
    strncpy(settings.wifi_ssid, _server.arg("wifi_ssid").c_str(), sizeof(settings.wifi_ssid) - 1);
    settings.wifi_ssid[sizeof(settings.wifi_ssid) - 1] = '\0';
  }

  if (_server.hasArg("wifi_password"))
  {
    strncpy(settings.wifi_password, _server.arg("wifi_password").c_str(), sizeof(settings.wifi_password) - 1);
    settings.wifi_password[sizeof(settings.wifi_password) - 1] = '\0';
  }

  // Save MQTT settings
  if (_server.hasArg("mqtt_server"))
  {
    strncpy(settings.mqtt_server, _server.arg("mqtt_server").c_str(), sizeof(settings.mqtt_server) - 1);
    settings.mqtt_server[sizeof(settings.mqtt_server) - 1] = '\0';
  }

  if (_server.hasArg("mqtt_port"))
  {
    settings.mqtt_port = _server.arg("mqtt_port").toInt();
  }

  if (_server.hasArg("mqtt_user"))
  {
    strncpy(settings.mqtt_user, _server.arg("mqtt_user").c_str(), sizeof(settings.mqtt_user) - 1);
    settings.mqtt_user[sizeof(settings.mqtt_user) - 1] = '\0';
  }

  if (_server.hasArg("mqtt_password"))
  {
    strncpy(settings.mqtt_password, _server.arg("mqtt_password").c_str(), sizeof(settings.mqtt_password) - 1);
    settings.mqtt_password[sizeof(settings.mqtt_password) - 1] = '\0';
  }

  // Save OTA password
  if (_server.hasArg("ota_password"))
  {
    strncpy(settings.ota_password, _server.arg("ota_password").c_str(), sizeof(settings.ota_password) - 1);
    settings.ota_password[sizeof(settings.ota_password) - 1] = '\0';
  }

  // Save timezone offset
  if (_server.hasArg("timezone_offset"))
  {
    int tz = _server.arg("timezone_offset").toInt();
    // Validate timezone offset (must be between -12 and +14)
    if (tz >= -12 && tz <= 14)
    {
      settings.timezone_offset = tz;
    }
  }

  // Save to EEPROM
  settings.save();

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Settings Saved</title></head><body>";
  html += "<h2>Settings Saved Successfully!</h2>";
  html += "<p>The device will restart in 3 seconds...</p>";
  html += "<script>setTimeout(function(){ window.location.href='/'; }, 3000);</script>";
  html += "</body></html>";

  _server.send(200, "text/html", html);

  // Restart device after a short delay
  delay(100);
  ESP.restart();
}

void WebServerHandler::handlePumpControl()
{
  if (!_server.hasArg("action"))
  {
    _server.send(400, "text/plain", "Missing action parameter");
    return;
  }

  String action = _server.arg("action");

  if (action == "on")
  {
    pumpController.setOverrideMode(true, true);
    Serial.println("Web: Pump override ON");
  }
  else if (action == "off")
  {
    pumpController.setOverrideMode(true, false);
    Serial.println("Web: Pump override OFF");
  }
  else if (action == "auto")
  {
    pumpController.setOverrideMode(false, false);
    Serial.println("Web: Pump set to AUTO mode");
  }
  else
  {
    _server.send(400, "text/plain", "Invalid action");
    return;
  }

  // Redirect back to home page
  _server.sendHeader("Location", "/");
  _server.send(303);
}

void WebServerHandler::handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: " + _server.uri() + "\n";
  message += "Method: " + String((_server.method() == HTTP_GET) ? "GET" : "POST") + "\n";
  _server.send(404, "text/plain", message);
}

String WebServerHandler::formatDuration(unsigned long milliseconds)
{
  if (milliseconds == 0)
  {
    return "Never";
  }

  unsigned long seconds = (millis() - milliseconds) / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;

  if (days > 0)
  {
    return String(days) + "d " + String(hours % 24) + "h ago";
  }
  else if (hours > 0)
  {
    return String(hours) + "h " + String(minutes % 60) + "m ago";
  }
  else if (minutes > 0)
  {
    return String(minutes) + "m " + String(seconds % 60) + "s ago";
  }
  else
  {
    return String(seconds) + "s ago";
  }
}

String WebServerHandler::formatPumpDuration(unsigned long milliseconds)
{
  if (milliseconds == 0)
  {
    return "Not available";
  }

  unsigned long seconds = milliseconds / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;

  if (days > 0)
  {
    return String(days) + "d " + String(hours % 24) + "h " + String(minutes % 60) + "m";
  }
  else if (hours > 0)
  {
    return String(hours) + "h " + String(minutes % 60) + "m " + String(seconds % 60) + "s";
  }
  else if (minutes > 0)
  {
    return String(minutes) + "m " + String(seconds % 60) + "s";
  }
  else
  {
    return String(seconds) + "s";
  }
}

String WebServerHandler::formatDateTime(time_t epoch)
{
  if (epoch <= 1000000000)
  {
    return "Not available";
  }

  // Apply timezone offset
  time_t local_time = epoch + (settings.timezone_offset * 3600);

  struct tm *timeinfo = gmtime(&local_time);
  char buffer[40];

  // Format timezone string (e.g., "GMT+7" or "GMT-5")
  char tz_str[10];
  if (settings.timezone_offset >= 0)
  {
    snprintf(tz_str, sizeof(tz_str), "GMT+%d", settings.timezone_offset);
  }
  else
  {
    snprintf(tz_str, sizeof(tz_str), "GMT%d", settings.timezone_offset);
  }

  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d %s",
           timeinfo->tm_year + 1900,
           timeinfo->tm_mon + 1,
           timeinfo->tm_mday,
           timeinfo->tm_hour,
           timeinfo->tm_min,
           timeinfo->tm_sec,
           tz_str);
  return String(buffer);
}

String WebServerHandler::buildStatusPage()
{
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Water Tank Controller</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }";
  html += "h2 { color: #333; }";
  html += ".container { display: flex; flex-wrap: wrap; gap: 20px; }";
  html += "table { border-collapse: collapse; width: 100%; max-width: 600px; background-color: white; }";
  html += "th, td { border: 1px solid #ddd; padding: 12px; text-align: left; }";
  html += "th { background-color: #4CAF50; color: white; }";
  html += "tr:nth-child(even) { background-color: #f2f2f2; }";
  html += ".status-on { color: green; font-weight: bold; }";
  html += ".status-off { color: red; font-weight: bold; }";
  html += ".button { display: inline-block; padding: 10px 20px; margin: 10px 5px; ";
  html += "background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px; border: none; cursor: pointer; }";
  html += ".button:hover { background-color: #45a049; }";
  html += ".button-danger { background-color: #f44336; }";
  html += ".button-danger:hover { background-color: #da190b; }";
  html += ".button-warning { background-color: #ff9800; }";
  html += ".button-warning:hover { background-color: #e68900; }";
  html += ".control-panel { background-color: white; padding: 20px; border-radius: 5px; margin: 20px 0; max-width: 600px; }";
  html += ".control-panel h3 { margin-top: 0; color: #333; }";
  html += ".control-buttons { display: flex; gap: 10px; flex-wrap: wrap; }";
  html += ".table-wrapper { flex: 1; min-width: 300px; }";
  html += "@media (max-width: 768px) { .container { flex-direction: column; } }";
  html += "</style>";
  html += "<script>";
  html += "var refreshTimer;";
  html += "function autoRefresh() { refreshTimer = setTimeout(function(){ location.reload(); }, 5000); }";
  html += "function stopRefresh() { clearTimeout(refreshTimer); }";
  html += "window.onload = autoRefresh;";
  html += "</script>";
  html += "</head><body>";

  html += "<h2>Water Tank Controller Status</h2>";
  html += "<p style='color: #666; font-size: 14px;'>Firmware Version: <strong>" + String(FIRMWARE_VERSION) + "</strong></p>";

  // Pump Control Panel
  html += "<div class='control-panel'>";
  html += "<h3>Pump Control</h3>";
  html += "<div class='control-buttons'>";
  html += "<form method='POST' action='/pump' style='display: inline;'>";
  html += "<input type='hidden' name='action' value='on'>";
  html += "<button type='submit' class='button button-danger' onclick='stopRefresh()'>Turn Pump ON</button>";
  html += "</form>";
  html += "<form method='POST' action='/pump' style='display: inline;'>";
  html += "<input type='hidden' name='action' value='off'>";
  html += "<button type='submit' class='button button-danger' onclick='stopRefresh()'>Turn Pump OFF</button>";
  html += "</form>";
  html += "<form method='POST' action='/pump' style='display: inline;'>";
  html += "<input type='hidden' name='action' value='auto'>";
  html += "<button type='submit' class='button button-warning' onclick='stopRefresh()'>Auto Mode</button>";
  html += "</form>";
  html += "</div>";
  html += "<p style='font-size: 12px; color: #666; margin-top: 10px;'>";
  html += "Current Mode: <strong>" + String(pumpController.isOverrideMode() ? "Manual Override" : "Automatic") + "</strong> | ";
  html += "Pump Status: <strong class='" + String(pumpController.getPumpState() ? "status-on" : "status-off") + "'>";
  html += pumpController.getPumpState() ? "ON" : "OFF";
  html += "</strong></p>";
  html += "</div>";

  html += "<div class='container'>";

  // General Status Table
  html += "<div class='table-wrapper'>";
  html += "<h3>General Status</h3>";
  html += "<table>";
  html += "<tr><th>Item</th><th>Status</th></tr>";

  // WiFi status
  html += "<tr><td>WiFi</td><td class='";
  html += systemManager.isWiFiConnected() ? "status-on'>Connected" : "status-off'>Disconnected";
  html += "</td></tr>";

  if (systemManager.isWiFiConnected())
  {
    html += "<tr><td>IP Address</td><td>" + systemManager.getIPAddress() + "</td></tr>";
  }

  // MQTT status
  html += "<tr><td>MQTT</td><td class='";
  html += mqttClient.isConnected() ? "status-on'>Connected" : "status-off'>Disconnected";
  html += "</td></tr>";

  // Water level sensors
  html += "<tr><td>Low Water Sensor</td><td class='";
  html += waterLevel.isLowWaterDetected() ? "status-on'>Active" : "status-off'>Inactive";
  html += "</td></tr>";

  html += "<tr><td>High Water Sensor</td><td class='";
  html += waterLevel.isHighWaterDetected() ? "status-on'>Active" : "status-off'>Inactive";
  html += "</td></tr>";

  // Pump status
  html += "<tr><td>Pump</td><td class='";
  html += pumpController.getPumpState() ? "status-on'>ON" : "status-off'>OFF";
  html += "</td></tr>";

  // Control mode
  html += "<tr><td>Control Mode</td><td>";
  html += pumpController.isOverrideMode() ? "Manual Override" : "Automatic";
  html += "</td></tr>";

  // Uptime
  unsigned long uptime = millis() / 1000;
  unsigned long hours = uptime / 3600;
  unsigned long minutes = (uptime % 3600) / 60;
  unsigned long seconds = uptime % 60;
  html += "<tr><td>Uptime</td><td>" + String(hours) + "h " + String(minutes) + "m " + String(seconds) + "s</td></tr>";

  html += "</table>";
  html += "</div>";

  // Pump Timing Table
  html += "<div class='table-wrapper'>";
  html += "<h3>Pump Timing</h3>";
  html += "<table>";
  html += "<tr><th>Event</th><th>Time</th></tr>";

  // Last pump ON
  html += "<tr><td>Last Pump ON</td><td>";
  html += formatDuration(pumpController.getLastOnTime());
  html += "</td></tr>";

  // Last pump ON timestamp
  html += "<tr><td>Last ON Timestamp</td><td>";
  html += formatDateTime(pumpController.getLastOnEpoch());
  html += "</td></tr>";

  // Last pump OFF
  html += "<tr><td>Last Pump OFF</td><td>";
  html += formatDuration(pumpController.getLastOffTime());
  html += "</td></tr>";

  // Last pump OFF timestamp
  html += "<tr><td>Last OFF Timestamp</td><td>";
  html += formatDateTime(pumpController.getLastOffEpoch());
  html += "</td></tr>";

  // Last pump duration (from last ON to OFF)
  html += "<tr><td>Last Pump Duration</td><td>";
  html += formatPumpDuration(pumpController.getLastPumpDuration());
  html += "</td></tr>";

  // Calculate pump running duration if currently ON
  if (pumpController.getPumpState() && pumpController.getLastOnTime() > 0)
  {
    unsigned long runDuration = (millis() - pumpController.getLastOnTime()) / 1000;
    unsigned long runHours = runDuration / 3600;
    unsigned long runMinutes = (runDuration % 3600) / 60;
    unsigned long runSeconds = runDuration % 60;
    html += "<tr><td>Current Run Duration</td><td>";
    html += String(runHours) + "h " + String(runMinutes) + "m " + String(runSeconds) + "s";
    html += "</td></tr>";
  }

  html += "</table>";
  html += "</div>";

  html += "</div>";

  html += "<br><a href='/setup' class='button' onclick='stopRefresh()'>Configure Settings</a>";
  html += "<a href='/update' class='button' onclick='stopRefresh()'>OTA Update</a>";

  html += "<p style='font-size: 12px; color: #666;'>Auto-refresh every 5 seconds (stops when clicking buttons)</p>";
  html += "</body></html>";

  return html;
}

String WebServerHandler::buildSetupPage()
{
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Configuration</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }";
  html += "form { background-color: white; padding: 20px; max-width: 500px; border-radius: 5px; }";
  html += "h2 { color: #333; }";
  html += "label { display: block; margin-top: 10px; font-weight: bold; }";
  html += "input { width: 100%; padding: 8px; margin-top: 5px; box-sizing: border-box; }";
  html += "input[type='submit'] { background-color: #4CAF50; color: white; border: none; ";
  html += "padding: 12px; margin-top: 20px; cursor: pointer; border-radius: 4px; }";
  html += "input[type='submit']:hover { background-color: #45a049; }";
  html += ".back-link { display: inline-block; margin-top: 20px; }";
  html += "</style></head><body>";

  html += "<form method='POST' action='/save'>";
  html += "<h2>WiFi Settings</h2>";
  html += "<label>SSID:</label><input type='text' name='wifi_ssid' value='" + String(settings.wifi_ssid) + "'>";
  html += "<label>Password:</label><input type='password' name='wifi_password' value='" + String(settings.wifi_password) + "'>";

  html += "<h2>MQTT Settings</h2>";
  html += "<label>Server:</label><input type='text' name='mqtt_server' value='" + String(settings.mqtt_server) + "'>";
  html += "<label>Port:</label><input type='number' name='mqtt_port' value='" + String(settings.mqtt_port) + "'>";
  html += "<label>Username:</label><input type='text' name='mqtt_user' value='" + String(settings.mqtt_user) + "'>";
  html += "<label>Password:</label><input type='password' name='mqtt_password' value='" + String(settings.mqtt_password) + "'>";

  html += "<h2>OTA Settings</h2>";
  html += "<label>OTA Password:</label><input type='password' name='ota_password' value='" + String(settings.ota_password) + "'>";

  html += "<h2>Time Settings</h2>";
  html += "<label>Timezone Offset (hours from UTC):</label>";
  html += "<input type='number' name='timezone_offset' min='-12' max='14' value='" + String(settings.timezone_offset) + "'>";
  html += "<small style='display: block; margin-top: 5px; color: #666;'>Examples: +7 for GMT+7 (Bangkok), -5 for EST, +0 for UTC</small>";

  html += "<input type='submit' value='Save Settings'>";
  html += "</form>";

  html += "<a href='/' class='back-link'>Back to Status</a>";
  html += "</body></html>";

  return html;
}

// ==================== GLOBAL INSTANCES ====================

Settings settings;
SystemManager systemManager;
WaterLevelSensor waterLevel;
PumpController pumpController;
MqttHandler mqttClient;
WebServerHandler webServer;

// ==================== ARDUINO SETUP & LOOP ====================

void setup()
{
  // Initialize system (WiFi, time sync, LED)
  systemManager.begin();

  // Initialize water level sensors
  waterLevel.begin();

  // Initialize pump controller
  pumpController.begin();

  // Initialize MQTT client
  mqttClient.begin();

  // Set up MQTT command callback for remote control
  mqttClient.setCommandCallback([](bool overrideMode, bool overrideState)
                                { pumpController.setOverrideMode(overrideMode, overrideState); });

  // Initialize web server
  webServer.begin();

  Serial.println("\n=== System Initialization Complete ===");
  Serial.println("Water Tank Controller is ready!");
}

void loop()
{
  // Feed watchdog timer
  yield();

  // Update system (time sync, WiFi status)
  systemManager.loop();
  yield();

  // Handle pump control logic
  pumpController.loop();
  yield();

  // Update LED status based on pump and override mode
  systemManager.updateLED(pumpController.getPumpState(), pumpController.isOverrideMode());
  yield();

  // Handle MQTT communication
  mqttClient.loop();
  yield();

  // Handle web server requests
  webServer.loop();
  yield();

  // Publish MQTT updates when pump state changes
  if (pumpController.hasPumpStateChanged())
  {
    mqttClient.publishPumpStatus(
        pumpController.getPumpState(),
        pumpController.getLastOnTime(),
        pumpController.getLastOffTime(),
        pumpController.getLastOnEpoch(),
        pumpController.getLastOffEpoch(),
        pumpController.getLastPumpDuration());
    yield();
  }

  // Publish MQTT updates when sensor state changes
  if (waterLevel.hasLowSensorChanged() || waterLevel.hasHighSensorChanged())
  {
    mqttClient.publishSensorStatus();
    waterLevel.resetChangeFlags();
    yield();
  }

  // Small delay to prevent CPU hogging
  delay(10);
}
