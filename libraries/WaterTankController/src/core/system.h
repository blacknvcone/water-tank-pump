#ifndef SYSTEM_H
#define SYSTEM_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <time.h>
#include "../config/settings.h"

class SystemManager {
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
  WiFiManager _wifiManager;
  bool _apMode;
  bool _timeSyncStarted;
  bool _timeSynced;
  unsigned long _ledBlinkTimer;
  bool _ledState;

  void checkTimeSync();
};

extern SystemManager systemManager;

#endif // SYSTEM_H
