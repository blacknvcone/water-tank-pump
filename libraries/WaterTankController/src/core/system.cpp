#include "system.h"

SystemManager systemManager;

SystemManager::SystemManager() :
  _apMode(false),
  _timeSyncStarted(false),
  _timeSynced(false),
  _ledBlinkTimer(0),
  _ledState(false) {
}

void SystemManager::begin() {
  Serial.begin(115200);
  Serial.println("\n\nWater Tank Controller v2.0");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // LED OFF (active low)

  // Load settings
  settings.begin();

  // Try to connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(settings.wifi_ssid, settings.wifi_password);

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nFailed to connect. Starting AP mode...");
    WiFi.softAP("WaterTank-Setup");
    _apMode = true;
    Serial.print("AP Started. IP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("\nConnected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    digitalWrite(LED_PIN, LOW); // LED ON when connected
    startTimeSync();
  }
}

void SystemManager::loop() {
  if (!_apMode && WiFi.status() == WL_CONNECTED) {
    if (!_timeSyncStarted) {
      startTimeSync();
    }
    if (!_timeSynced) {
      checkTimeSync();
    }
  }
}

bool SystemManager::isWiFiConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

bool SystemManager::isAPMode() const {
  return _apMode;
}

String SystemManager::getIPAddress() const {
  if (_apMode) {
    return WiFi.softAPIP().toString();
  }
  return WiFi.localIP().toString();
}

bool SystemManager::isTimeSynced() const {
  return _timeSynced;
}

time_t SystemManager::getCurrentTime() const {
  return time(nullptr);
}

void SystemManager::startTimeSync() {
  if (_timeSyncStarted) return;

  Serial.println("Starting time sync with NTP...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  _timeSyncStarted = true;
}

void SystemManager::checkTimeSync() {
  time_t now = time(nullptr);
  if (now > 1000000000) { // Valid timestamp (after 2001)
    _timeSynced = true;
    Serial.print("Time synced: ");
    Serial.println(ctime(&now));
  }
}

void SystemManager::updateLED(bool pumpState, bool overrideMode) {
  if (!isWiFiConnected()) {
    // Blink fast when disconnected
    if (millis() - _ledBlinkTimer > 200) {
      _ledState = !_ledState;
      digitalWrite(LED_PIN, _ledState ? LOW : HIGH);
      _ledBlinkTimer = millis();
    }
  } else if (overrideMode) {
    // Blink slowly in override mode
    if (millis() - _ledBlinkTimer > 1000) {
      _ledState = !_ledState;
      digitalWrite(LED_PIN, _ledState ? LOW : HIGH);
      _ledBlinkTimer = millis();
    }
  } else if (pumpState) {
    // Solid on when pump is running
    digitalWrite(LED_PIN, LOW);
  } else {
    // Solid on when connected and idle
    digitalWrite(LED_PIN, LOW);
  }
}
