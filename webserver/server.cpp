#include "server.h"

WebServerHandler webServer;

WebServerHandler::WebServerHandler() : _server(80) {
}

void WebServerHandler::begin() {
  // Setup routes
  _server.on("/", [this]() { handleRoot(); });
  _server.on("/setup", [this]() { handleSetup(); });
  _server.on("/save", HTTP_POST, [this]() { handleSave(); });
  _server.onNotFound([this]() { handleNotFound(); });

  // Setup OTA update handler
  _httpUpdater.setup(&_server, "/update", settings.ota_password);

  _server.begin();
  Serial.println("Web server started on port 80");
  Serial.print("Access at: http://");
  Serial.println(systemManager.getIPAddress());
}

void WebServerHandler::loop() {
  _server.handleClient();
}

void WebServerHandler::handleRoot() {
  String html = buildStatusPage();
  _server.send(200, "text/html", html);
}

void WebServerHandler::handleSetup() {
  String html = buildSetupPage();
  _server.send(200, "text/html", html);
}

void WebServerHandler::handleSave() {
  // Save WiFi settings
  if (_server.hasArg("wifi_ssid")) {
    strncpy(settings.wifi_ssid, _server.arg("wifi_ssid").c_str(), sizeof(settings.wifi_ssid) - 1);
    settings.wifi_ssid[sizeof(settings.wifi_ssid) - 1] = '\0';
  }

  if (_server.hasArg("wifi_password")) {
    strncpy(settings.wifi_password, _server.arg("wifi_password").c_str(), sizeof(settings.wifi_password) - 1);
    settings.wifi_password[sizeof(settings.wifi_password) - 1] = '\0';
  }

  // Save MQTT settings
  if (_server.hasArg("mqtt_server")) {
    strncpy(settings.mqtt_server, _server.arg("mqtt_server").c_str(), sizeof(settings.mqtt_server) - 1);
    settings.mqtt_server[sizeof(settings.mqtt_server) - 1] = '\0';
  }

  if (_server.hasArg("mqtt_port")) {
    settings.mqtt_port = _server.arg("mqtt_port").toInt();
  }

  if (_server.hasArg("mqtt_user")) {
    strncpy(settings.mqtt_user, _server.arg("mqtt_user").c_str(), sizeof(settings.mqtt_user) - 1);
    settings.mqtt_user[sizeof(settings.mqtt_user) - 1] = '\0';
  }

  if (_server.hasArg("mqtt_password")) {
    strncpy(settings.mqtt_password, _server.arg("mqtt_password").c_str(), sizeof(settings.mqtt_password) - 1);
    settings.mqtt_password[sizeof(settings.mqtt_password) - 1] = '\0';
  }

  // Save OTA password
  if (_server.hasArg("ota_password")) {
    strncpy(settings.ota_password, _server.arg("ota_password").c_str(), sizeof(settings.ota_password) - 1);
    settings.ota_password[sizeof(settings.ota_password) - 1] = '\0';
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

void WebServerHandler::handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: " + _server.uri() + "\n";
  message += "Method: " + String((_server.method() == HTTP_GET) ? "GET" : "POST") + "\n";
  _server.send(404, "text/plain", message);
}

String WebServerHandler::buildStatusPage() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Water Tank Controller</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }";
  html += "h2 { color: #333; }";
  html += "table { border-collapse: collapse; width: 100%; max-width: 600px; background-color: white; }";
  html += "th, td { border: 1px solid #ddd; padding: 12px; text-align: left; }";
  html += "th { background-color: #4CAF50; color: white; }";
  html += "tr:nth-child(even) { background-color: #f2f2f2; }";
  html += ".status-on { color: green; font-weight: bold; }";
  html += ".status-off { color: red; font-weight: bold; }";
  html += ".button { display: inline-block; padding: 10px 20px; margin: 10px 0; ";
  html += "background-color: #4CAF50; color: white; text-decoration: none; border-radius: 4px; }";
  html += ".button:hover { background-color: #45a049; }";
  html += "</style>";
  html += "<script>";
  html += "function autoRefresh() { setTimeout(function(){ location.reload(); }, 5000); }";
  html += "window.onload = autoRefresh;";
  html += "</script>";
  html += "</head><body>";

  html += "<h2>Water Tank Controller Status</h2>";
  html += "<table>";
  html += "<tr><th>Item</th><th>Status</th></tr>";

  // WiFi status
  html += "<tr><td>WiFi</td><td class='";
  html += systemManager.isWiFiConnected() ? "status-on'>Connected" : "status-off'>Disconnected";
  html += "</td></tr>";

  if (systemManager.isWiFiConnected()) {
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

  html += "<br><a href='/setup' class='button'>Configure Settings</a>";
  html += "<a href='/update' class='button'>OTA Update</a>";

  html += "<p style='font-size: 12px; color: #666;'>Auto-refresh every 5 seconds</p>";
  html += "</body></html>";

  return html;
}

String WebServerHandler::buildSetupPage() {
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

  html += "<input type='submit' value='Save Settings'>";
  html += "</form>";

  html += "<a href='/' class='back-link'>Back to Status</a>";
  html += "</body></html>";

  return html;
}
