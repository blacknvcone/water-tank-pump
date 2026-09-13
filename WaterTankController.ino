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
#include <RtcDS1302.h>

// ==================== CONFIGURATION ====================

// Firmware version
#define FIRMWARE_VERSION "2.2.0"

// EEPROM addresses and size
#define EEPROM_SIZE 256
#define WIFI_SSID_ADDR 100
#define WIFI_PASS_ADDR 140
#define MQTT_ADDR 0
#define OTA_PASS_ADDR 200
#define TIMEZONE_ADDR 220
#define LAST_BOOT_TIME_ADDR 224    // Store last boot time (4 bytes)
#define LAST_MILLIS_ADDR 228       // Store millis at last time sync (4 bytes)
#define TIME_SYNC_INTERVAL_ADDR 232 // Store time sync interval (1 byte)

// GPIO PIN definitions
#define LOW_SENSOR_PIN 4  // D2
#define HIGH_SENSOR_PIN 5 // D1
#define RELAY_PIN 14      // D5
#define LED_PIN 2         // D4 (Built-in LED, active low)

// DS1302 RTC Pin definitions
#define RTC_CLK_PIN 12  // D6
#define RTC_DAT_PIN 13  // D7
#define RTC_CE_PIN  0   // D3

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


// ==================== RTC MANAGER CLASS ====================

class RtcManager
{
public:
  RtcManager();

  void begin();

  // Time operations
  time_t getRtcTime() const;
  bool isRtcValid() const;
  void setRtcTime(time_t t);
  void syncFromNtp(time_t ntpTime);

  // Diagnostics
  const char* getBatteryHealth() const;

private:
  ThreeWire _wire;
  mutable RtcDS1302<ThreeWire> _rtc; // mutable: GetDateTime() not const in library
  bool _initialized;
  bool _rtcAvailable;
  long _lastDriftSeconds;
};

RtcManager::RtcManager() : _wire(RTC_DAT_PIN, RTC_CLK_PIN, RTC_CE_PIN),
                           _rtc(_wire),
                           _initialized(false),
                           _rtcAvailable(false),
                           _lastDriftSeconds(0)
{
}

void RtcManager::begin()
{
  Serial.println("Initializing DS1302 RTC...");

  Serial.print("RTC pins - CLK:");
  Serial.print(RTC_CLK_PIN);
  Serial.print(" DAT:");
  Serial.print(RTC_DAT_PIN);
  Serial.print(" CE:");
  Serial.println(RTC_CE_PIN);

  _rtc.Begin();

  _rtc.SetIsWriteProtected(false);
  Serial.println("Write protect disabled");

  _rtc.SetIsRunning(true);
  Serial.println("Clock halted bit cleared");

  RtcDateTime dt = _rtc.GetDateTime();
  Serial.print("Raw read - Year:");
  Serial.print(dt.Year());
  Serial.print(" Month:");
  Serial.print(dt.Month());
  Serial.print(" Day:");
  Serial.print(dt.Day());
  Serial.print(" Hour:");
  Serial.print(dt.Hour());
  Serial.print(" Min:");
  Serial.print(dt.Minute());
  Serial.print(" Sec:");
  Serial.println(dt.Second());

  if (dt.IsValid())
  {
    _rtcAvailable = true;
    _initialized = true;

    time_t rtcTime = dt.Epoch32Time();
    Serial.print("RTC time: ");
    char buf[30];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             dt.Year(), dt.Month(), dt.Day(),
             dt.Hour(), dt.Minute(), dt.Second());
    Serial.println(buf);
  }
  else
  {
    Serial.println("RTC time invalid. Check wiring:");
    Serial.println("  CLK -> D6 (GPIO12)");
    Serial.println("  DAT -> D7 (GPIO13)");
    Serial.println("  RST -> D3 (GPIO0)");
    Serial.println("  VCC -> 3.3V (try 5V if still failing)");
    Serial.println("  GND -> GND");
    _rtcAvailable = true;
    _initialized = true;
  }
}

time_t RtcManager::getRtcTime() const
{
  if (!_initialized || !_rtcAvailable)
    return 0;

  RtcDateTime dt = _rtc.GetDateTime();
  if (dt.IsValid())
  {
    return dt.Epoch32Time();
  }
  return 0;
}

bool RtcManager::isRtcValid() const
{
  return _initialized && _rtcAvailable && getRtcTime() > 1000000000;
}

void RtcManager::setRtcTime(time_t t)
{
  if (!_initialized || !_rtcAvailable || t <= 1000000000)
    return;

  RtcDateTime dt;
  dt.InitWithEpoch32Time(t);
  _rtc.SetDateTime(dt);

  Serial.print("RTC updated: ");
  Serial.println(ctime(&t));
}

void RtcManager::syncFromNtp(time_t ntpTime)
{
  if (ntpTime > 1000000000)
  {
    time_t rtcTime = getRtcTime();
    if (rtcTime > 1000000000)
    {
      _lastDriftSeconds = (long)(ntpTime - rtcTime);
      Serial.print("RTC drift from NTP: ");
      Serial.print(_lastDriftSeconds);
      Serial.println(" seconds");
    }

    setRtcTime(ntpTime);
  }
}

const char* RtcManager::getBatteryHealth() const
{
  if (!_initialized || !_rtcAvailable)
    return "unknown";

  time_t rtcTime = getRtcTime();
  if (rtcTime <= 1000000000)
    return "dead";

  if (_lastDriftSeconds != 0)
  {
    long absDrift = _lastDriftSeconds < 0 ? -_lastDriftSeconds : _lastDriftSeconds;
    if (absDrift > 300)
      return "weak";
  }

  return "good";
}

RtcManager rtcManager;

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
  void saveTimeToEEPROM(time_t currentTime);
  void loadTimeFromEEPROM();
  bool isValidTime(time_t t) const;

  // LED management
  void updateLED(bool pumpState, bool overrideMode);

private:
  bool _apMode;
  bool _timeSyncStarted;
  bool _timeSynced;
  bool _useCompensatedTime;    // Use millis-based compensation when NTP unavailable
  time_t _lastKnownTime;       // Last known good time from NTP or EEPROM
  unsigned long _lastMillis;   // millis() at last time sync/check
  unsigned long _ledBlinkTimer;
  bool _ledState;

  void checkTimeSync();
  void updateCompensatedTime();
};

SystemManager::SystemManager() : _apMode(false),
                                 _timeSyncStarted(false),
                                 _timeSynced(false),
                                 _useCompensatedTime(false),
                                 _lastKnownTime(0),
                                 _lastMillis(0),
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

  // Initialize RTC first (instant time, no network needed)
  rtcManager.begin();

  // Load time: RTC is primary, EEPROM is fallback
  time_t rtcTime = rtcManager.getRtcTime();
  if (rtcTime > 1000000000)
  {
    _lastKnownTime = rtcTime + (settings.timezone_offset * 3600);
    _lastMillis = millis();
    _useCompensatedTime = true;
    Serial.print("Time from RTC (local): ");
    Serial.println(ctime(&_lastKnownTime));
  }
  else
  {
    Serial.println("RTC unavailable, falling back to EEPROM time");
    loadTimeFromEEPROM();
  }

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
  // Update compensated time even without network
  updateCompensatedTime();

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
  // First try to get NTP time if synced
  if (_timeSynced)
  {
    time_t ntpTime = time(nullptr);
    if (isValidTime(ntpTime))
    {
      return ntpTime;
    }
  }

  // Fall back to compensated time if available
  if (_useCompensatedTime && _lastKnownTime > 0)
  {
    return _lastKnownTime;
  }

  // Last resort - return system time (may be invalid)
  return time(nullptr);
}

void SystemManager::startTimeSync()
{
  if (_timeSyncStarted)
    return;

  Serial.println("Starting time sync with NTP...");
  Serial.print("Timezone offset: GMT+");
  Serial.println(settings.timezone_offset);

  // Set TZ environment variable directly (POSIX format)
  // POSIX convention: the offset is local->UTC, so UTC-7 = GMT+7
  char tzStr[16];
  snprintf(tzStr, sizeof(tzStr), "UTC%d", -settings.timezone_offset);
  setenv("TZ", tzStr, 1);
  tzset();
  Serial.print("TZ set to: ");
  Serial.println(tzStr);

  configTime(settings.timezone_offset * 3600, 0, "pool.ntp.org", "time.nist.gov");
  _timeSyncStarted = true;

  // Set current millis for compensation
  _lastMillis = millis();
}

void SystemManager::checkTimeSync()
{
  time_t now = time(nullptr);
  if (isValidTime(now))
  {
    if (!_timeSynced)
    {
      _timeSynced = true;
      _useCompensatedTime = false;
      _lastKnownTime = now;
      _lastMillis = millis();

      Serial.print("Time synced: ");
      Serial.println(ctime(&now));

      // Convert to UTC for storage in RTC and EEPROM
      time_t utcNow = now - (settings.timezone_offset * 3600);

      // Save valid time to EEPROM for persistence (UTC)
      saveTimeToEEPROM(utcNow);

      // Sync NTP time to RTC for persistence (UTC)
      rtcManager.syncFromNtp(utcNow);
    }
    else
    {
      // Time is still valid, update our tracking
      _lastKnownTime = now;
      _lastMillis = millis();
    }
  }
  else
  {
    // NTP time not available, check if we should use compensated time
    if (!_useCompensatedTime && _lastKnownTime > 0)
    {
      Serial.println("NTP time unavailable, using compensated time from EEPROM");
      _useCompensatedTime = true;
    }
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

// ==================== TIME SYNC HELPER METHODS ====================

bool SystemManager::isValidTime(time_t t) const
{
  return (t > 1000000000); // After September 2001
}

void SystemManager::saveTimeToEEPROM(time_t currentTime)
{
  if (!isValidTime(currentTime))
    return;

  // Save the last known good time and current millis
  EEPROM.put(LAST_BOOT_TIME_ADDR, currentTime);
  EEPROM.put(LAST_MILLIS_ADDR, millis());

  // Mark that we have valid time saved
  uint8_t timeValid = 1;
  EEPROM.put(TIME_SYNC_INTERVAL_ADDR, timeValid);

  EEPROM.commit();
  Serial.println("Time saved to EEPROM for persistence");
}

void SystemManager::loadTimeFromEEPROM()
{
  // Check if we have previously saved time
  uint8_t timeValid;
  EEPROM.get(TIME_SYNC_INTERVAL_ADDR, timeValid);

  if (timeValid == 1)
  {
    time_t savedTime;
    unsigned long savedMillis;

    EEPROM.get(LAST_BOOT_TIME_ADDR, savedTime);
    EEPROM.get(LAST_MILLIS_ADDR, savedMillis);

    if (isValidTime(savedTime))
    {
      // Calculate time elapsed since last save
      unsigned long elapsedMillis = millis() - savedMillis;
      time_t elapsedSeconds = elapsedMillis / 1000;

      // EEPROM stores UTC - convert to local time
      _lastKnownTime = savedTime + elapsedSeconds + (settings.timezone_offset * 3600);
      _lastMillis = millis();
      _useCompensatedTime = true;

      Serial.print("Loaded time from EEPROM: ");
      Serial.print(ctime(&_lastKnownTime));
      Serial.print("Time elapsed since last save: ");
      Serial.print(elapsedSeconds);
      Serial.println(" seconds");
    }
  }
  else
  {
    Serial.println("No valid time found in EEPROM, waiting for NTP sync");
    _useCompensatedTime = false;
    _lastKnownTime = 0;
    _lastMillis = 0;
  }
}

void SystemManager::updateCompensatedTime()
{
  if (_useCompensatedTime && _lastKnownTime > 0)
  {
    // Calculate elapsed time since last update
    unsigned long currentMillis = millis();
    unsigned long elapsedMillis = currentMillis - _lastMillis;

    // Update compensated time every second
    if (elapsedMillis >= 1000)
    {
      time_t elapsedSeconds = elapsedMillis / 1000;
      _lastKnownTime += elapsedSeconds;
      _lastMillis = currentMillis;

      // Save updated time to EEPROM periodically (every 24 hours, RTC is primary)
      static unsigned long lastSaveTime = 0;
      if (currentMillis - lastSaveTime > 86400000) // 24 hours
      {
        // Convert to UTC for storage
        time_t utcTime = _lastKnownTime - (settings.timezone_offset * 3600);
        saveTimeToEEPROM(utcTime);
        rtcManager.syncFromNtp(utcTime); // Keep RTC accurate
        lastSaveTime = currentMillis;
      }
    }
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
  // Update epoch timestamps if time is valid and we have boot timestamps
  if (currentTime > 1000000000) // Use same validation as SystemManager
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
  StaticJsonDocument<576> doc;

  // Add firmware version
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["rtc"] = rtcManager.isRtcValid();
  doc["battery_health"] = rtcManager.getBatteryHealth();

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

  
  // 12. RTC Status (Binary Sensor)
  {
    String topic = String(HA_DISCOVERY_PREFIX) + "/binary_sensor/" + deviceId + "_rtc/config";
    String payload = "{";
    payload += "\"name\":\"Water Tank RTC Status\",";
    payload += "\"unique_id\":\"" + uniqueIdBase + "_rtc\",";
    payload += "\"state_topic\":\"" + String(MQTT_STATE_TOPIC) + "\",";
    payload += "\"value_template\":\"{{ value_json.rtc }}\",";
    payload += "\"payload_on\":true,";
    payload += "\"payload_off\":false,";
    payload += "\"icon\":\"mdi:chip\",";
    payload += deviceInfo;
    payload += "}";
    _client.publish(topic.c_str(), payload.c_str(), true);
    yield();
  }

  // 13. RTC Battery Health (Sensor)
  {
    String topic = String(HA_DISCOVERY_PREFIX) + "/sensor/" + deviceId + "_battery_health/config";
    String payload = "{";
    payload += "\"name\":\"Water Tank RTC Battery\",";
    payload += "\"unique_id\":\"" + uniqueIdBase + "_battery_health\",";
    payload += "\"state_topic\":\"" + String(MQTT_STATE_TOPIC) + "\",";
    payload += "\"value_template\":\"{{ value_json.battery_health }}\",";
    payload += "\"icon\":\"mdi:battery\",";
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
  void handleApiStatus();
  void handleSetup();
  void handleSave();
  void handlePumpControl();
  void handleTimeSync();
  void handleRestart();
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
  _server.on("/api/status", HTTP_GET, [this]()
             { handleApiStatus(); });
  _server.on("/setup", [this]()
             { handleSetup(); });
  _server.on("/save", HTTP_POST, [this]()
             { handleSave(); });
  _server.on("/pump", HTTP_POST, [this]()
             { handlePumpControl(); });
  _server.on("/timesync", HTTP_POST, [this]()
             { handleTimeSync(); });
  _server.on("/restart", HTTP_POST, [this]()
             { handleRestart(); });
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


void WebServerHandler::handleApiStatus()
{
  String json = "{";
  json += "\"firmware\":\"" + String(FIRMWARE_VERSION) + "\",";
  json += "\"wifi\":" + String(systemManager.isWiFiConnected() ? "true" : "false") + ",";
  json += "\"ip\":\"" + systemManager.getIPAddress() + "\",";
  json += "\"mqtt\":" + String(mqttClient.isConnected() ? "true" : "false") + ",";
  json += "\"time_synced\":" + String(systemManager.isTimeSynced() ? "true" : "false") + ",";
  json += "\"rtc_valid\":" + String(rtcManager.isRtcValid() ? "true" : "false") + ",";
  json += "\"battery_health\":\"" + String(rtcManager.getBatteryHealth()) + "\",";
  json += "\"low_water\":" + String(waterLevel.isLowWaterDetected() ? "true" : "false") + ",";
  json += "\"high_water\":" + String(waterLevel.isHighWaterDetected() ? "true" : "false") + ",";
  json += "\"pump\":" + String(pumpController.getPumpState() ? "true" : "false") + ",";
  json += "\"override_mode\":" + String(pumpController.isOverrideMode() ? "true" : "false") + ",";
  json += "\"override_state\":" + String(pumpController.getOverrideState() ? "true" : "false") + ",";
  unsigned long uptime = millis() / 1000;
  json += "\"uptime\":" + String(uptime) + ",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"pump_last_on\":" + String(pumpController.getLastOnTime()) + ",";
  json += "\"pump_last_off\":" + String(pumpController.getLastOffTime()) + ",";
  json += "\"pump_duration\":" + String(pumpController.getLastPumpDuration()) + ",";
  time_t now = systemManager.getCurrentTime();
  json += "\"current_time\":" + String((unsigned long)now);
  json += "}";
  _server.send(200, "application/json", json);
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

void WebServerHandler::handleTimeSync()
{
  Serial.println("Web: Manual time sync requested");

  // Check if WiFi is connected
  if (!systemManager.isWiFiConnected())
  {
    _server.send(400, "text/plain", "WiFi not connected. Time sync requires internet access.");
    return;
  }

  // Force restart time sync
  systemManager.startTimeSync();

  // Check immediately if time sync works
  delay(2000); // Give NTP some time to respond

  time_t currentTime = systemManager.getCurrentTime();
  if (currentTime > 1000000000) // Use same validation as SystemManager
  {
    String response = "Time sync successful!\n\n";
    response += "Current time: ";

    char timeStr[50];
    struct tm *timeinfo = gmtime(&currentTime);
    if (timeinfo)
    {
      snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02d %02d:%02d:%02d UTC",
               timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
               timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
      response += timeStr;
    }
    else
    {
      response += String(currentTime) + " (epoch)";
    }

    response += "\n\nRedirecting to status page...";
    _server.send(200, "text/plain", response);
  }
  else
  {
    _server.send(503, "text/plain", "Time sync failed. NTP servers may be unreachable. Please try again later.");
  }

  // Redirect back to home page after a delay
  _server.sendHeader("Refresh", "3; url=/");
}

void WebServerHandler::handleRestart()
{
  Serial.println("Web: Device restart requested");

  // Send response page first
  String html = "<!DOCTYPE html><html><head><title>Restarting Device</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 40px; background-color: #f5f5f5; }";
  html += ".container { max-width: 500px; margin: 0 auto; background-color: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); text-align: center; }";
  html += ".icon { font-size: 48px; margin-bottom: 20px; }";
  html += ".message { font-size: 18px; margin: 20px 0; color: #333; }";
  html += ".countdown { font-size: 24px; font-weight: bold; color: #f44336; margin: 15px 0; }";
  html += ".progress { width: 100%; height: 8px; background-color: #e0e0e0; border-radius: 4px; overflow: hidden; margin: 20px 0; }";
  html += ".progress-bar { height: 100%; background-color: #4CAF50; transition: width 1s linear; }";
  html += "</style>";
  html += "<script>";
  html += "let countdown = 30;";
  html += "function updateCountdown() {";
  html += "  countdown--;";
  html += "  document.getElementById('countdown').textContent = countdown;";
  html += "  document.getElementById('progress').style.width = ((30-countdown)/30*100) + '%';";
  html += "  if (countdown <= 0) {";
  html += "    document.getElementById('status').innerHTML = 'Device should be restarted now.<br>Redirecting...';";
  html += "    setTimeout(function() { window.location.href = '/'; }, 2000);";
  html += "  } else {";
  html += "    setTimeout(updateCountdown, 1000);";
  html += "  }";
  html += "}";
  html += "window.onload = function() { updateCountdown(); };";
  html += "</script>";
  html += "</head><body>";
  html += "<div class='container'>";
  html += "<div class='icon'>🔄</div>";
  html += "<h2>Device Restarting</h2>";
  html += "<p class='message'>The Water Tank Controller is restarting...</p>";
  html += "<p class='message'>Please wait for the device to come back online.</p>";
  html += "<div class='countdown' id='countdown'>30</div>";
  html += "<p class='message'>seconds remaining</p>";
  html += "<div class='progress'><div class='progress-bar' id='progress' style='width: 0%'></div></div>";
  html += "<p id='status' style='color: #666; font-size: 14px;'>Do not close this page. It will automatically redirect when the device is back online.</p>";
  html += "</div></body></html>";

  _server.send(200, "text/html", html);

  // Delay briefly to ensure response is sent
  delay(100);

  // Restart the ESP8266
  Serial.println("Restarting ESP8266 now...");
  ESP.restart();
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
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Water Tank Controller</title>";
  html += "<style>";
  html += "*{margin:0;padding:0;box-sizing:border-box}";
  html += ":root{--bg:#f5f7fa;--card:#fff;--text:#1a1a2e;--dim:#6b7280;--border:#e5e7eb;--green:#10b981;--red:#ef4444;--amber:#f59e0b;--blue:#3b82f6}";
  html += "@media(prefers-color-scheme:dark){:root{--bg:#0f172a;--card:#1e293b;--text:#e2e8f0;--dim:#94a3b8;--border:#334155}}";
  html += "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',system-ui,sans-serif;background:var(--bg);color:var(--text);padding:16px;max-width:800px;margin:0 auto}";
  html += "h1{font-size:1.4rem;margin-bottom:4px}";
  html += ".sub{color:var(--dim);font-size:.8rem;margin-bottom:16px}";
  html += ".card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:16px;margin-bottom:12px}";
  html += ".card h2{font-size:1rem;margin-bottom:12px;display:flex;align-items:center;gap:8px}";
  html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px}";
  html += ".stat{display:flex;flex-direction:column;gap:2px}";
  html += ".stat .label{font-size:.75rem;color:var(--dim);text-transform:uppercase;letter-spacing:.5px}";
  html += ".stat .value{font-size:1.1rem;font-weight:600}";
  html += ".dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px}";
  html += ".dot.on{background:var(--green)}.dot.off{background:var(--red)}.dot.warn{background:var(--amber)}.dot.idle{background:var(--dim)}";
  html += ".pump-btns{display:flex;gap:8px;flex-wrap:wrap}";
  html += ".btn{padding:10px 18px;border:none;border-radius:8px;font-size:.85rem;font-weight:600;cursor:pointer;transition:all .15s}";
  html += ".btn:active{transform:scale(.97)}";
  html += ".btn-on{background:var(--green);color:#fff}.btn-on:hover{opacity:.85}";
  html += ".btn-off{background:var(--red);color:#fff}.btn-off:hover{opacity:.85}";
  html += ".btn-auto{background:var(--blue);color:#fff}.btn-auto:hover{opacity:.85}";
  html += ".btn-sm{padding:6px 14px;font-size:.78rem;background:var(--border);color:var(--text);border-radius:6px;text-decoration:none;display:inline-block}";
  html += ".btn-sm:hover{opacity:.8}";
  html += ".mode-tag{display:inline-block;padding:2px 8px;border-radius:4px;font-size:.75rem;font-weight:600}";
  html += ".mode-auto{background:#d1fae5;color:#065f46}.mode-manual{background:#fef3c7;color:#92400e}";
  html += "@media(prefers-color-scheme:dark){.mode-auto{background:#064e3b;color:#6ee7b7}.mode-manual{background:#78350f;color:#fde68a}}";
  html += ".ts{color:var(--dim);font-size:.75rem;margin-top:8px}";
  html += "</style></head><body>";

  html += "<h1>Water Tank Controller</h1>";
  html += "<p class='sub'>v" + String(FIRMWARE_VERSION) + " &middot; <span id='clock'>--</span></p>";

  // Pump Control Card
  html += "<div class='card'>";
  html += "<h2><span class='dot' id='d-pump'></span>Pump Control</h2>";
  html += "<div class='pump-btns'>";
  html += "<button class='btn btn-on' onclick='pump(\"on\")'>Turn ON</button>";
  html += "<button class='btn btn-off' onclick='pump(\"off\")'>Turn OFF</button>";
  html += "<button class='btn btn-auto' onclick='pump(\"auto\")'>Auto Mode</button>";
  html += "</div>";
  html += "<p class='ts'>Mode: <span id='mode-tag'></span> &middot; Status: <strong id='pump-status'>--</strong></p>";
  html += "</div>";

  // System Status Card
  html += "<div class='card'>";
  html += "<h2>System Status</h2>";
  html += "<div class='grid'>";
  html += "<div class='stat'><span class='label'>WiFi</span><span class='value' id='s-wifi'>--</span></div>";
  html += "<div class='stat'><span class='label'>MQTT</span><span class='value' id='s-mqtt'>--</span></div>";
  html += "<div class='stat'><span class='label'>Time Sync</span><span class='value' id='s-time'>--</span></div>";
  html += "<div class='stat'><span class='label'>RTC</span><span class='value' id='s-rtc'>--</span></div>";
  html += "<div class='stat'><span class='label'>RTC Battery</span><span class='value' id='s-batt'>--</span></div>";
  html += "<div class='stat'><span class='label'>Uptime</span><span class='value' id='s-uptime'>--</span></div>";
  html += "<div class='stat'><span class='label'>WiFi Signal</span><span class='value' id='s-rssi'>--</span></div>";
  html += "<div class='stat'><span class='label'>IP Address</span><span class='value' id='s-ip'>--</span></div>";
  html += "</div></div>";

  // Water Level Card
  html += "<div class='card'>";
  html += "<h2>Water Level</h2>";
  html += "<div class='grid'>";
  html += "<div class='stat'><span class='label'>Low Sensor</span><span class='value' id='s-low'>--</span></div>";
  html += "<div class='stat'><span class='label'>High Sensor</span><span class='value' id='s-high'>--</span></div>";
  html += "</div></div>";

  // Pump History Card
  html += "<div class='card'>";
  html += "<h2>Pump History</h2>";
  html += "<div class='grid'>";
  html += "<div class='stat'><span class='label'>Last ON</span><span class='value' id='h-on'>--</span></div>";
  html += "<div class='stat'><span class='label'>Last OFF</span><span class='value' id='h-off'>--</span></div>";
  html += "<div class='stat'><span class='label'>Last Duration</span><span class='value' id='h-dur'>--</span></div>";
  html += "<div class='stat'><span class='label'>Running</span><span class='value' id='h-run'>--</span></div>";
  html += "</div></div>";

  // Footer links
  html += "<div style='display:flex;gap:8px;margin-top:8px;flex-wrap:wrap'>";
  html += "<a class='btn-sm' href='/setup'>Settings</a>";
  html += "<a class='btn-sm' href='/update'>OTA Update</a>";
  html += "<button class='btn-sm' onclick=\"if(confirm('Restart device?'))fetch('/restart',{method:'POST'})\">Restart</button>";
  html += "</div>";

  // JavaScript - AJAX polling
  html += "<script>";
  html += "function fmt(s){if(!s||s<1)return'-';var h=Math.floor(s/3600),m=Math.floor(s%3600/60),ss=s%60;return h?h+'h '+m+'m':m?m+'m '+ss+'s':ss+'s'}";
  html += "function fmtDur(ms){if(!ms)return'-';var s=Math.floor(ms/1000),h=Math.floor(s/3600),m=Math.floor(s%3600/60),ss=s%60;return h?h+'h '+m+'m '+ss+'s':m?m+'m '+ss+'s':ss+'s'}";
  html += "function dot(el,ok){el.className='dot '+(ok?'on':'off')}";
  html += "function val(el,txt,ok){el.textContent=txt;el.style.color=ok?'var(--green)':ok===false?'var(--red)':'var(--text)'}";
  html += "function dt(ep){if(!ep||ep<1000000000)return'-';var d=new Date(ep*1000);return d.toLocaleDateString('en-GB',{day:'2-digit',month:'short'})+' '+d.toLocaleTimeString('en-GB',{hour:'2-digit',minute:'2-digit'})}";
  html += "function poll(){fetch('/api/status').then(r=>r.json()).then(d=>{";
  html += "document.getElementById('clock').textContent=dt(d.current_time);";
  html += "val(document.getElementById('s-wifi'),d.wifi?'Connected':'Disconnected',d.wifi);";
  html += "val(document.getElementById('s-mqtt'),d.mqtt?'Connected':'Disconnected',d.mqtt);";
  html += "val(document.getElementById('s-time'),d.time_synced?'NTP Synced':'Not Synced',d.time_synced);";
  html += "val(document.getElementById('s-rtc'),d.rtc_valid?'Active':'Missing',d.rtc_valid);";
  html += "val(document.getElementById('s-batt'),d.battery_health,d.battery_health==='good'?true:d.battery_health==='weak'?null:false);";
  html += "document.getElementById('s-uptime').textContent=fmt(d.uptime);";
  html += "document.getElementById('s-rssi').textContent=d.rssi+' dBm';";
  html += "document.getElementById('s-ip').textContent=d.ip;";
  html += "val(document.getElementById('s-low'),d.low_water?'Active':'Inactive',d.low_water);";
  html += "val(document.getElementById('s-high'),d.high_water?'Active':'Inactive',d.high_water);";
  html += "val(document.getElementById('pump-status'),d.pump?'ON':'OFF',d.pump);";
  html += "dot(document.getElementById('d-pump'),d.pump);";
  html += "var mt=document.getElementById('mode-tag');mt.textContent=d.override_mode?'Manual':'Automatic';";
  html += "mt.className='mode-tag '+(d.override_mode?'mode-manual':'mode-auto');";
  html += "if(d.pump_last_on>0){document.getElementById('h-on').textContent=fmt(Math.floor(d.uptime-d.pump_last_on/1000))+' ago'}else{document.getElementById('h-on').textContent='Never'}";
  html += "if(d.pump_last_off>0){document.getElementById('h-off').textContent=fmt(Math.floor(d.uptime-d.pump_last_off/1000))+' ago'}else{document.getElementById('h-off').textContent='Never'}";
  html += "document.getElementById('h-dur').textContent=fmtDur(d.pump_duration);";
  html += "if(d.pump&&d.pump_last_on>0){var run=Math.floor((Date.now()/1000-d.pump_last_on/1000));document.getElementById('h-run').textContent=fmt(run)}else{document.getElementById('h-run').textContent='--'}";
  html += "}).catch(()=>{})}";
  html += "poll();setInterval(poll,3000);";
  html += "function pump(a){fetch('/pump',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'action='+a}).then(()=>setTimeout(poll,300))}";
  html += "</body></html>";

  return html;
}

String WebServerHandler::buildSetupPage()
{
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Settings</title>";
  html += "<style>";
  html += "*{margin:0;padding:0;box-sizing:border-box}";
  html += ":root{--bg:#f5f7fa;--card:#fff;--text:#1a1a2e;--dim:#6b7280;--border:#e5e7eb;--green:#10b981;--blue:#3b82f6}";
  html += "@media(prefers-color-scheme:dark){:root{--bg:#0f172a;--card:#1e293b;--text:#e2e8f0;--dim:#94a3b8;--border:#334155}}";
  html += "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',system-ui,sans-serif;background:var(--bg);color:var(--text);padding:16px;max-width:500px;margin:0 auto}";
  html += "h1{font-size:1.4rem;margin-bottom:16px}";
  html += ".card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:16px;margin-bottom:12px}";
  html += ".card h2{font-size:.9rem;color:var(--dim);text-transform:uppercase;letter-spacing:.5px;margin-bottom:12px}";
  html += "label{display:block;font-size:.85rem;font-weight:600;margin-bottom:4px}";
  html += "input[type=text],input[type=password],input[type=number]{width:100%;padding:10px 12px;border:1px solid var(--border);border-radius:8px;font-size:.9rem;background:var(--bg);color:var(--text);margin-bottom:12px}";
  html += "input:focus{outline:none;border-color:var(--blue);box-shadow:0 0 0 2px rgba(59,130,246,.2)}";
  html += ".hint{font-size:.75rem;color:var(--dim);margin-top:-8px;margin-bottom:12px}";
  html += ".btn{display:block;width:100%;padding:12px;border:none;border-radius:8px;font-size:.9rem;font-weight:600;cursor:pointer;margin-top:8px}";
  html += ".btn-primary{background:var(--green);color:#fff}.btn-primary:hover{opacity:.85}";
  html += ".btn-back{background:transparent;border:1px solid var(--border);color:var(--text);text-align:center;text-decoration:none;margin-top:12px}";
  html += ".btn-back:hover{opacity:.7}";
  html += "</style></head><body>";

  html += "<h1>Settings</h1>";
  html += "<form method='POST' action='/save'>";

  html += "<div class='card'>";
  html += "<h2>WiFi</h2>";
  html += "<label>SSID</label><input type='text' name='wifi_ssid' value='" + String(settings.wifi_ssid) + "'>";
  html += "<label>Password</label><input type='password' name='wifi_password' value='" + String(settings.wifi_password) + "'>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h2>MQTT</h2>";
  html += "<label>Server</label><input type='text' name='mqtt_server' value='" + String(settings.mqtt_server) + "'>";
  html += "<label>Port</label><input type='number' name='mqtt_port' value='" + String(settings.mqtt_port) + "'>";
  html += "<label>Username</label><input type='text' name='mqtt_user' value='" + String(settings.mqtt_user) + "'>";
  html += "<label>Password</label><input type='password' name='mqtt_password' value='" + String(settings.mqtt_password) + "'>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h2>System</h2>";
  html += "<label>OTA Password</label><input type='password' name='ota_password' value='" + String(settings.ota_password) + "'>";
  html += "<label>Timezone (hours from UTC)</label>";
  html += "<input type='number' name='timezone_offset' min='-12' max='14' value='" + String(settings.timezone_offset) + "'>";
  html += "<div class='hint'>+7 for GMT+7 (WIB), -5 for EST, +0 for UTC</div>";
  html += "</div>";

  html += "<button class='btn btn-primary' type='submit'>Save Settings</button>";
  html += "</form>";
  html += "<a class='btn btn-back' href='/'>Back to Status</a>";
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