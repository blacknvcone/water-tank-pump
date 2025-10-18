#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include <EEPROM.h>

// EEPROM addresses and size
#define EEPROM_SIZE 256
#define WIFI_SSID_ADDR 100
#define WIFI_PASS_ADDR 140
#define MQTT_ADDR 0
#define OTA_PASS_ADDR 200

// GPIO PIN definitions
#define LOW_SENSOR_PIN 4  // D2
#define HIGH_SENSOR_PIN 5 // D1
#define RELAY_PIN 14      // D5
#define LED_PIN 2         // D4 (Built-in LED, active low)

// MQTT topics
#define DEVICE_ID "water_tank_controller"
#define MQTT_STATE_TOPIC "zigbee2mqtt/" DEVICE_ID
#define MQTT_COMMAND_TOPIC "zigbee2mqtt/" DEVICE_ID "/set"
#define MQTT_AVAILABILITY_TOPIC "zigbee2mqtt/bridge/state"

class Settings {
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
  
  // Check if MQTT is configured
  bool isMqttConfigured() const;

private:
  bool _mqttConfigured;
};

extern Settings settings;

#endif // SETTINGS_H