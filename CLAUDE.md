# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a modular ESP8266-based IoT water tank pump controller with automatic water level management, MQTT integration, and web-based configuration. The project is structured as a single Arduino IDE sketch (`WaterTankController.ino`) containing all modular classes.

## Development Environment

### Required Setup
- **Arduino IDE** (1.8.x or 2.x) with ESP8266 board support
- **Board**: NodeMCU 1.0 (ESP-12E Module)
- **Upload Speed**: 115200
- **CPU Frequency**: 80 MHz
- **Flash Size**: 4MB (FS:2MB OTA:~1019KB)

### Required Libraries
- `PubSubClient` by Nick O'Leary
- `ArduinoJson` by Benoit Blanchon (v6.x)
- `WiFiManager` by tzapu

### Build and Upload
```bash
# Open in Arduino IDE and use Upload button (→)
# Or use arduino-cli if installed:
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 .
arduino-cli upload --fqbn esp8266:esp8266:nodemcuv2 --port /dev/ttyUSB0 .
```

### Serial Monitor
- **Baud Rate**: 115200
- Use for debugging system status, WiFi connection, MQTT connectivity, and sensor readings

## Code Architecture

### Single File Structure
All code is contained in `WaterTankController.ino` (1860+ lines) with the following modular classes:

1. **Settings** (lines ~51-193)
   - EEPROM-based configuration management
   - WiFi credentials, MQTT settings, OTA password, timezone offset
   - `begin()`, `load()`, `save()` methods

2. **SystemManager** (lines ~202-378)
   - WiFi connection management via WiFiManager
   - NTP time synchronization
   - LED status indicators with priority-based patterns
   - `begin()`, `loop()` methods

3. **WaterLevelSensor** (lines ~379-482)
   - Debounced dual sensor reading (low/high water level)
   - State change detection and filtering
   - `begin()`, `readSensors()`, `hasStateChanged()` methods

4. **PumpController** (lines ~483-664)
   - Automatic pump control with hysteresis logic
   - Manual override mode via MQTT commands
   - Timestamp tracking for pump on/off events
   - `begin()`, `loop()`, `turnOn()`, `turnOff()`, `setOverride()` methods

5. **MqttHandler** (lines ~665-1020)
   - Zigbee2MQTT protocol implementation
   - State publishing and command handling
   - Home Assistant auto-discovery support
   - Connection management and reconnection logic

6. **WebServerHandler** (lines ~1021-1860)
   - ESP8266WebServer-based interface
   - Status monitoring with auto-refresh
   - Configuration forms for WiFi/MQTT/OTA settings
   - OTA update functionality with HTTP authentication

### Global Objects
```cpp
Settings settings;
SystemManager systemManager;
WaterLevelSensor waterLevel;
PumpController pumpController;
MqttHandler mqttClient;
WebServerHandler webServer;
```

### Key Constants
- **GPIO Pins**: LOW_SENSOR_PIN (4/D2), HIGH_SENSOR_PIN (5/D1), RELAY_PIN (14/D5), LED_PIN (2/D4)
- **EEPROM Layout**: Settings stored at specific addresses (0-255)
- **MQTT Topics**: `watertank/water_tank_controller` (state), `watertank/water_tank_controller/set` (commands)
- **LED Patterns**: Very fast blink (pump ON), very slow blink (WiFi disconnected), solid ON (normal)

## Control Logic

### Automatic Mode
- Low water detected (contact=false) → Pump turns ON
- High water detected (water_leak=true) → Pump turns OFF
- Between levels → Maintain current state (hysteresis)

### Manual Override
- MQTT command: `{"override":true,"state":"ON"}` or `{"override":true,"state":"OFF"}`
- Web interface: ON/OFF/Auto buttons
- Override persists until explicitly disabled

## Configuration Flow

1. **First Boot**: Creates WiFi AP "WaterTank-Setup" at 192.168.4.1
2. **Web Setup**: Navigate to `http://192.168.4.1` for initial configuration
3. **Normal Operation**: Device connects to WiFi, starts MQTT, serves web interface
4. **OTA Updates**: Available at `http://<device-ip>/update` (admin/ota_password)

## Common Development Tasks

### Adding New MQTT Commands
1. Update `MqttHandler::handleCommand()` to parse new JSON fields
2. Add corresponding methods to relevant controller classes
3. Update state publishing to include new data
4. Modify Home Assistant discovery configuration if needed

### Modifying Web Interface
1. Update HTML templates in `WebServerHandler::handleRoot()` and related methods
2. Add new routes in `WebServerHandler::begin()`
3. Update form handling in `WebServerHandler::handleSetup()`

### Changing Sensor Logic
1. Modify `WaterLevelSensor::readSensors()` for new sensor types
2. Update pin definitions and debounce logic
3. Adjust pump control logic in `PumpController::loop()`

### Debugging Tips
- Use Serial Monitor at 115200 baud for real-time debugging
- Check LED patterns for quick status diagnosis
- MQTT messages can be monitored with `mosquitto_sub` for troubleshooting
- Web interface shows detailed sensor states and pump status

## Safety Considerations

This code controls physical hardware (water pumps) and electrical systems:
- Always verify relay wiring and power ratings
- Include physical emergency cutoff switches
- Test with low-voltage systems first
- Follow local electrical codes and regulations
- Ensure proper waterproofing for outdoor installations