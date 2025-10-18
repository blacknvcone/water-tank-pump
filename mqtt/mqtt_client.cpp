#include "mqtt_client.h"

MqttHandler mqttClient;
MqttHandler* MqttHandler::_instance = nullptr;

MqttHandler::MqttHandler() :
  _client(_wifiClient),
  _lastStatusUpdate(0) {
  _instance = this;
}

void MqttHandler::begin() {
  if (!settings.isMqttConfigured()) {
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

void MqttHandler::loop() {
  if (!settings.isMqttConfigured()) {
    return;
  }

  if (!_client.connected()) {
    connect();
  }

  if (_client.connected()) {
    _client.loop();

    // Periodically publish state updates
    if (millis() - _lastStatusUpdate > _statusUpdateInterval) {
      publishState();
      _lastStatusUpdate = millis();
    }
  }
}

bool MqttHandler::isConnected() const {
  return _client.connected();
}

bool MqttHandler::connect() {
  if (_client.connected()) {
    return true;
  }

  Serial.print("Connecting to MQTT...");

  String clientId = String(DEVICE_ID) + "_" + String(ESP.getChipId(), HEX);

  bool connected = false;
  if (strlen(settings.mqtt_user) > 0) {
    connected = _client.connect(clientId.c_str(), settings.mqtt_user, settings.mqtt_password);
  } else {
    connected = _client.connect(clientId.c_str());
  }

  if (connected) {
    Serial.println("Connected!");

    // Subscribe to command topic
    _client.subscribe(MQTT_COMMAND_TOPIC);
    Serial.print("Subscribed to: ");
    Serial.println(MQTT_COMMAND_TOPIC);

    // Publish initial state
    publishState();

    return true;
  } else {
    Serial.print("Failed, rc=");
    Serial.println(_client.state());
    return false;
  }
}

void MqttHandler::publishState() {
  if (!_client.connected()) {
    return;
  }

  // Create JSON document
  StaticJsonDocument<512> doc;

  // Add sensor states
  doc["contact"] = waterLevel.isLowWaterDetected();
  doc["water_leak"] = waterLevel.isHighWaterDetected();

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
                                     time_t lastOnEpoch, time_t lastOffEpoch) {
  if (!_client.connected()) {
    return;
  }

  StaticJsonDocument<512> doc;

  doc["state"] = pumpState ? "ON" : "OFF";

  // Add timestamps if available
  if (lastOnEpoch > 1000000000) {
    char isoTime[30];
    if (formatISO8601(lastOnEpoch, isoTime, sizeof(isoTime))) {
      doc["last_on"] = isoTime;
    }
  }

  if (lastOffEpoch > 1000000000) {
    char isoTime[30];
    if (formatISO8601(lastOffEpoch, isoTime, sizeof(isoTime))) {
      doc["last_off"] = isoTime;
    }
  }

  // Add runtime info
  doc["runtime_last_on"] = lastOnTime;
  doc["runtime_last_off"] = lastOffTime;

  char buffer[512];
  serializeJson(doc, buffer);

  String pumpTopic = String(MQTT_STATE_TOPIC) + "/pump";
  _client.publish(pumpTopic.c_str(), buffer, true);

  Serial.print("Published pump status: ");
  Serial.println(buffer);
}

void MqttHandler::publishSensorStatus() {
  // This is included in the main publishState() method
  publishState();
}

void MqttHandler::setCommandCallback(CommandCallback callback) {
  _commandCallback = callback;
}

bool MqttHandler::formatISO8601(time_t t, char* out, size_t len) {
  struct tm* timeinfo = gmtime(&t);
  if (!timeinfo) {
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

void MqttHandler::mqttCallbackWrapper(char* topic, byte* payload, unsigned int length) {
  if (_instance) {
    _instance->mqttCallback(topic, payload, length);
  }
}

void MqttHandler::mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("MQTT message received on topic: ");
  Serial.println(topic);

  // Parse JSON payload
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    Serial.print("JSON parse failed: ");
    Serial.println(error.c_str());
    return;
  }

  // Check if this is a command for our device
  if (strcmp(topic, MQTT_COMMAND_TOPIC) == 0) {
    // Handle override mode commands
    if (doc.containsKey("override")) {
      bool overrideMode = doc["override"].as<bool>();
      bool overrideState = false;

      if (doc.containsKey("state")) {
        String state = doc["state"].as<String>();
        overrideState = (state == "ON");
      }

      // Call the callback if set
      if (_commandCallback) {
        _commandCallback(overrideMode, overrideState);
      }

      Serial.print("Override command: mode=");
      Serial.print(overrideMode);
      Serial.print(", state=");
      Serial.println(overrideState ? "ON" : "OFF");
    }
  }
}
