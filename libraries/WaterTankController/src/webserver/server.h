#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include "../config/settings.h"
#include "../core/system.h"
#include "../sensors/water_level.h"
#include "../pump/controller.h"
#include "../mqtt/mqtt_client.h"

class WebServerHandler {
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
  void handleNotFound();

  // Helper methods
  String buildStatusPage();
  String buildSetupPage();
};

extern WebServerHandler webServer;

#endif // WEBSERVER_H
