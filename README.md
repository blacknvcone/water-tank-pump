# Water Tank Pump Controller v2.0

An intelligent IoT-based water tank pump controller with automatic water level management, MQTT integration, and web-based configuration.

## Features

- **Automatic Pump Control**: Intelligent pump control based on water level sensors with hysteresis
- **MQTT Integration**: Full Zigbee2MQTT protocol support for home automation integration
- **Web Interface**: Easy-to-use web interface for configuration and monitoring with real-time status
- **Manual Control**: Web-based pump override buttons (ON/OFF/Auto) for instant control
- **OTA Updates**: Over-the-air firmware updates for easy maintenance
- **Remote Control**: MQTT command support for home automation integration
- **Time Logging**: Tracks pump on/off timestamps with configurable timezone (GMT+7 default)
- **Visual Feedback**: Priority-based LED status indicators for pump and WiFi state
- **Modular Architecture**: Clean, maintainable code structure

## Hardware Requirements

### Components
- **ESP8266 NodeMCU v2** (or compatible board)
- **2x Water Level Sensors** (float switches or similar)
- **Relay Module** (5V compatible, for pump control)
- **Water Pump** (appropriate for your tank size)
- **Power Supply** (5V for ESP8266, appropriate voltage for pump)

### Pin Configuration

```
GPIO Pin    NodeMCU Pin    Function
--------    -----------    --------
GPIO 2      D4             Built-in LED (status indicator)
GPIO 4      D2             Low water level sensor
GPIO 5      D1             High water level sensor
GPIO 14     D5             Relay control (pump)
```

## Software Requirements

### Arduino IDE Setup

1. Install [Arduino IDE](https://www.arduino.cc/en/software) (1.8.x or 2.x)

2. Add ESP8266 Board Support:
   - Open Arduino IDE
   - Go to **File → Preferences**
   - Add to "Additional Board Manager URLs":
     ```
     http://arduino.esp8266.com/stable/package_esp8266com_index.json
     ```
   - Go to **Tools → Board → Board Manager**
   - Search for "esp8266" and install

3. Install Required Libraries:
   - Go to **Sketch → Include Library → Manage Libraries**
   - Install the following libraries:
     - `PubSubClient` by Nick O'Leary
     - `ArduinoJson` by Benoit Blanchon (v6.x)
     - `WiFiManager` by tzapu

### Board Configuration

- **Board**: NodeMCU 1.0 (ESP-12E Module)
- **Upload Speed**: 115200
- **CPU Frequency**: 80 MHz
- **Flash Size**: 4MB (FS:2MB OTA:~1019KB)
- **Port**: Select your USB port

## Installation

1. Download or clone this repository
2. Open `WaterTankController.ino` in Arduino IDE
3. Install required libraries (see Software Requirements above)
4. Select correct board and port under **Tools** menu
5. Click **Upload** button (→)

## Configuration

### First-Time Setup

1. After flashing, the device will create a WiFi access point named **"WaterTank-Setup"**
2. Connect to this AP with your phone/computer
3. Navigate to `http://192.168.4.1` in your browser
4. Go to **Configure Settings**
5. Enter your WiFi credentials and MQTT settings
6. Click **Save Settings**
7. The device will restart and connect to your network

### Web Interface

Once connected to your network, access the web interface at the device's IP address:

- **Status Page**: `http://<device-ip>/`
  - Real-time system status (WiFi, MQTT, sensors, pump)
  - Pump timing information with timestamps
  - Manual pump control buttons (ON/OFF/Auto)
  - Auto-refresh every 5 seconds

- **Configuration**: `http://<device-ip>/setup`
  - WiFi settings (SSID, password)
  - MQTT broker settings (server, port, credentials)
  - OTA password configuration
  - Timezone offset configuration (default: GMT+7)

- **OTA Updates**: `http://<device-ip>/update`
  - Upload new firmware (.bin files)
  - Username: `admin`
  - Password: configured OTA password (default: `astalavista`)

### MQTT Configuration

The controller follows the Zigbee2MQTT protocol for easy integration with home automation systems:

**Topics:**
- State: `zigbee2mqtt/water_tank_controller`
- Commands: `zigbee2mqtt/water_tank_controller/set`
- Availability: `zigbee2mqtt/bridge/state`

**State Message Format:**
```json
{
  "contact": false,        // Low water sensor (true = water detected)
  "water_leak": true,      // High water sensor (true = water detected)
  "linkquality": 180       // WiFi signal strength (0-255)
}
```

**Command Message Format:**
```json
{
  "override": true,        // Enable manual override
  "state": "ON"           // Pump state ("ON" or "OFF")
}
```

**Pump Status Topic:** `zigbee2mqtt/water_tank_controller/pump`
```json
{
  "state": "ON",
  "last_on": "2025-10-19T12:30:45Z",
  "last_off": "2025-10-19T11:15:30Z",
  "runtime_last_on": 1234567,
  "runtime_last_off": 1234000
}
```

## Project Structure

```
water-tank-pump/
├── WaterTankController.ino       # Main program file (open this in Arduino IDE)
├── config/
│   ├── settings.h                # Configuration management
│   └── settings.cpp
├── core/
│   ├── system.h                  # System initialization & WiFi
│   └── system.cpp
├── sensors/
│   ├── water_level.h             # Water level sensor handling
│   └── water_level.cpp
├── pump/
│   ├── controller.h              # Pump control logic
│   └── controller.cpp
├── mqtt/
│   ├── mqtt_client.h             # MQTT communication
│   └── mqtt_client.cpp
├── webserver/
│   ├── server.h                  # Web interface
│   └── server.cpp
└── README.md                     # This file
```

## Module Architecture

The project follows a modular design for maintainability:

### Core Modules

1. **System Manager** (`core/system.*`)
   - WiFi connection management
   - NTP time synchronization
   - LED status indicators
   - System initialization

2. **Settings** (`config/settings.*`)
   - EEPROM-based configuration storage
   - WiFi credentials
   - MQTT connection settings
   - OTA password management

3. **Water Level Sensor** (`sensors/water_level.*`)
   - Debounced sensor reading
   - State change detection
   - Dual sensor support (low/high)

4. **Pump Controller** (`pump/controller.*`)
   - Automatic pump control logic
   - Manual override mode
   - Timestamp tracking
   - State management

5. **MQTT Client** (`mqtt/mqtt_client.*`)
   - Zigbee2MQTT protocol implementation
   - State publishing
   - Command handling
   - Connection management

6. **Web Server** (`webserver/server.*`)
   - Status monitoring interface
   - Configuration web forms
   - OTA update handler
   - Real-time status updates

## Operation

### Automatic Mode (Default)

The pump operates automatically based on water level:

1. **Low water detected** → Pump turns ON
2. **High water detected** → Pump turns OFF
3. **Between levels** → Pump maintains current state (hysteresis)

### Manual Override Mode

#### Web Interface Control

The easiest way to control the pump is via the web interface:

1. Navigate to `http://<device-ip>/`
2. Use the **Pump Control** panel at the top:
   - **Turn Pump ON**: Forces pump to run continuously
   - **Turn Pump OFF**: Forces pump to stop
   - **Auto Mode**: Returns to automatic sensor-based control
3. Current mode and pump status are displayed in real-time
4. Page auto-refreshes every 5 seconds

#### MQTT Control

Control the pump remotely via MQTT commands:

```bash
# Turn pump ON (override)
mosquitto_pub -t "zigbee2mqtt/water_tank_controller/set" \
  -m '{"override":true,"state":"ON"}'

# Turn pump OFF (override)
mosquitto_pub -t "zigbee2mqtt/water_tank_controller/set" \
  -m '{"override":true,"state":"OFF"}'

# Return to automatic mode
mosquitto_pub -t "zigbee2mqtt/water_tank_controller/set" \
  -m '{"override":false}'
```

### LED Status Indicators

The built-in LED (D4/GPIO2) provides visual feedback about the system state with priority-based patterns:

| LED Pattern | Blink Speed | System State | Description |
|-------------|-------------|--------------|-------------|
| **Very Fast Blink** | 100ms (10 blinks/sec) | **Pump Running** | Pump is actively ON (highest priority - overrides all other states) |
| **Very Slow Blink** | 1000ms (1 blink/sec) | **WiFi Disconnected** | Not connected to WiFi or in AP mode (when pump is OFF) |
| **Solid ON** | No blink | **Normal Operation** | WiFi connected, pump is OFF, all systems normal |

**Priority Order:**
1. **Pump ON** always shows very fast blink, regardless of WiFi status
2. **WiFi status** only shown when pump is OFF
3. This makes it easy to identify pump activity at a glance

## Troubleshooting

### Device won't connect to WiFi
- Check SSID and password in configuration
- Ensure WiFi is 2.4GHz (ESP8266 doesn't support 5GHz)
- Factory reset by reflashing firmware

### MQTT not connecting
- Verify MQTT broker is running and accessible
- Check MQTT credentials
- Ensure port 1883 is not blocked
- Check broker logs for connection attempts

### Pump not responding
- Verify relay wiring and power
- Check sensor readings on status page
- Test manual override via MQTT
- Check serial monitor for debug output

### Web interface not accessible
- Verify device is connected to WiFi (check router DHCP table)
- Try accessing via IP address shown in serial monitor
- Ensure device and computer are on same network

## Serial Monitor Debug

Connect via serial at **115200 baud** to see debug output:

```
Water Tank Controller v2.0
Connecting to WiFi...
Connected!
IP Address: 192.168.1.100
Starting time sync with NTP...
MQTT configured for: 192.168.1.50:1883
Pump controller initialized
Web server started on port 80
=== System Initialization Complete ===
Water Tank Controller is ready!
```

## Home Assistant Integration

Add to your `configuration.yaml`:

```yaml
mqtt:
  sensor:
    - name: "Water Tank Low Level"
      state_topic: "zigbee2mqtt/water_tank_controller"
      value_template: "{{ value_json.contact }}"
      device_class: moisture

    - name: "Water Tank High Level"
      state_topic: "zigbee2mqtt/water_tank_controller"
      value_template: "{{ value_json.water_leak }}"
      device_class: moisture

    - name: "Water Tank Pump"
      state_topic: "zigbee2mqtt/water_tank_controller/pump"
      value_template: "{{ value_json.state }}"

  switch:
    - name: "Water Pump Override"
      command_topic: "zigbee2mqtt/water_tank_controller/set"
      state_topic: "zigbee2mqtt/water_tank_controller/pump"
      value_template: "{{ value_json.state }}"
      payload_on: '{"override":true,"state":"ON"}'
      payload_off: '{"override":false}'
```

## Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues.

## License

This project is open source and available under the MIT License.

## Author

Dani Prasetya

## Version History

- **v2.0** - Modular refactoring with improved maintainability
  - Separated concerns into distinct modules
  - Added comprehensive documentation
  - Improved error handling
  - Enhanced MQTT integration
  - Arduino IDE focused structure

- **v1.x** - Initial monolithic implementation
  - Basic pump control
  - MQTT support
  - Web configuration

## Safety Notes

⚠️ **Important Safety Information:**

- Always use appropriate electrical isolation for mains-powered pumps
- Ensure relay ratings exceed pump current requirements
- Use waterproof enclosures for outdoor installations
- Include manual pump cutoff switch for emergencies
- Regular maintenance and testing recommended
- Follow local electrical codes and regulations
