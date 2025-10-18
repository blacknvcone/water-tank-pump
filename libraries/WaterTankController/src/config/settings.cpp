#include "settings.h"

Settings settings;

Settings::Settings() : 
  mqtt_port(1883),
  _mqttConfigured(false) {
  
  // Initialize strings to empty
  wifi_ssid[0] = '\0';
  wifi_password[0] = '\0';
  mqtt_server[0] = '\0';
  mqtt_user[0] = '\0';
  mqtt_password[0] = '\0';
  
  // Default OTA password
  strcpy(ota_password, "the_password");
}

void Settings::begin() {
  load();
}

void Settings::load() {
  EEPROM.begin(EEPROM_SIZE);

  EEPROM.get(WIFI_SSID_ADDR, wifi_ssid);
  EEPROM.get(WIFI_PASS_ADDR, wifi_password);
  EEPROM.get(MQTT_ADDR, mqtt_server);
  EEPROM.get(MQTT_ADDR + 40, mqtt_user);
  EEPROM.get(MQTT_ADDR + 60, mqtt_password);
  EEPROM.get(MQTT_ADDR + 80, mqtt_port);
  EEPROM.get(OTA_PASS_ADDR, ota_password);

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

void Settings::save() {
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
  
  _mqttConfigured = (strlen(mqtt_server) > 0);
}

bool Settings::isMqttConfigured() const {
  return _mqttConfigured;
}