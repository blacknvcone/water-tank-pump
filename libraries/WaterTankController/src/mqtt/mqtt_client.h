#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "../config/settings.h"
#include "../sensors/water_level.h"

class MqttHandler {
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
                          time_t lastOnEpoch, time_t lastOffEpoch);
  void publishSensorStatus();
  
  // Command callback handling
  typedef std::function<void(bool overrideMode, bool overrideState)> CommandCallback;
  void setCommandCallback(CommandCallback callback);
  
private:
  WiFiClient _wifiClient;
  PubSubClient _client;
  CommandCallback _commandCallback;
  unsigned long _lastStatusUpdate;
  const long _statusUpdateInterval = 10000; // 10 seconds
  
  // Format time as ISO8601
  bool formatISO8601(time_t t, char* out, size_t len);
  
  // Static callback wrapper for PubSubClient
  static void mqttCallbackWrapper(char* topic, byte* payload, unsigned int length);
  static MqttHandler* _instance;
  
  // Actual callback implementation
  void mqttCallback(char* topic, byte* payload, unsigned int length);
};

extern MqttHandler mqttClient;

#endif // MQTT_CLIENT_H