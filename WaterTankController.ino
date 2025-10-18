#include <Arduino.h>
#include <core/system.h>
#include <sensors/water_level.h>
#include <mqtt/mqtt_client.h>
#include <webserver/server.h>
#include <config/settings.h>
#include <pump/controller.h>

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

void setup() {
  // Initialize system (WiFi, time sync, LED)
  systemManager.begin();

  // Initialize water level sensors
  waterLevel.begin();

  // Initialize pump controller
  pumpController.begin();

  // Initialize MQTT client
  mqttClient.begin();

  // Set up MQTT command callback for remote control
  mqttClient.setCommandCallback([](bool overrideMode, bool overrideState) {
    pumpController.setOverrideMode(overrideMode, overrideState);
  });

  // Initialize web server
  webServer.begin();

  Serial.println("\n=== System Initialization Complete ===");
  Serial.println("Water Tank Controller is ready!");
}

void loop() {
  // Update system (time sync, WiFi status)
  systemManager.loop();

  // Handle pump control logic
  pumpController.loop();

  // Update LED status based on pump and override mode
  systemManager.updateLED(pumpController.getPumpState(), pumpController.isOverrideMode());

  // Handle MQTT communication
  mqttClient.loop();

  // Handle web server requests
  webServer.loop();

  // Publish MQTT updates when pump state changes
  if (pumpController.hasPumpStateChanged()) {
    mqttClient.publishPumpStatus(
      pumpController.getPumpState(),
      pumpController.getLastOnTime(),
      pumpController.getLastOffTime(),
      pumpController.getLastOnEpoch(),
      pumpController.getLastOffEpoch()
    );
  }

  // Publish MQTT updates when sensor state changes
  if (waterLevel.hasLowSensorChanged() || waterLevel.hasHighSensorChanged()) {
    mqttClient.publishSensorStatus();
    waterLevel.resetChangeFlags();
  }

  // Small delay to prevent CPU hogging
  delay(10);
}