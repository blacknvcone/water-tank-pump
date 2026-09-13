# Quick Start Guide

## 🚀 Getting Started in 5 Minutes

### 1. Install Arduino IDE
Download and install from: https://www.arduino.cc/en/software

### 2. Add ESP8266 Support
1. Open Arduino IDE
2. Go to **File → Preferences**
3. Add this URL to "Additional Board Manager URLs":
   ```
   http://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
4. Go to **Tools → Board → Board Manager**
5. Search "esp8266" and click **Install**

### 3. Install Libraries
1. Go to **Sketch → Include Library → Manage Libraries**
2. Install these three libraries:
   - `PubSubClient`
   - `ArduinoJson` (v6.x)
   - `WiFiManager`

### 4. Open Project
1. Navigate to this project folder
2. Double-click `WaterTankController.ino`
3. Arduino IDE will open with all files

### 5. Configure Board
In Arduino IDE, go to **Tools** menu and set:
- **Board**: "NodeMCU 1.0 (ESP-12E Module)"
- **Upload Speed**: 115200
- **Port**: Select your USB port (COM3, /dev/ttyUSB0, etc.)

### 6. Upload
1. Connect your ESP8266 via USB
2. Click the **Upload** button (→)
3. Wait for "Done uploading"

### 7. Configure via Web
1. Device creates WiFi AP: **"WaterTank-Setup"**
2. Connect to it
3. Open browser to `http://192.168.4.1`
4. Enter your WiFi and MQTT settings
5. Save and restart

### 8. Done! 🎉
Check Serial Monitor (115200 baud) to see the device IP address.
Access web interface at `http://<device-ip>`

---

## Hardware Connections

```
ESP8266 NodeMCU          Component
---------------          ---------
D2 (GPIO4)      →       Low Water Sensor
D1 (GPIO5)      →       High Water Sensor
D5 (GPIO14)     →       Relay IN (for pump)
GND             →       Common Ground
3.3V            →       Sensor Power (if needed)
```

## RTC Module (Optional but Recommended)

Connect DS1302N RTC module (5-pin):
```
VCC  → 3.3V
GND  → GND
CLK  → D6 (GPIO12)
DAT  → D7 (GPIO13)
RST  → D3 (GPIO0)
```
Insert CR2032 battery into the module's onboard holder.

## Need Help?
See the full **README.md** for detailed documentation.
