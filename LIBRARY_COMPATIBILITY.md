# ESP32 Library Compatibility - Working Configuration

## ✅ Tested & Working Setup

This configuration has been tested and works with the **LilyGO T-Display-S3** board.

### Arduino IDE Configuration

**Board Settings:**
- Board: **ESP32S3 Dev Module** or **LilyGO T-Display-S3**
- USB CDC On Boot: **Enabled**
- USB Mode: **Hardware CDC and JTAG**
- Upload Speed: **921600**
- Partition Scheme: **Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)**

### ESP32 Board Manager Version

**Important:** Use **Arduino ESP32 version < 3.0**

✅ **Working:** Arduino ESP32 2.x.x (recommended: 2.0.11 or 2.0.14)
❌ **Not Working:** Arduino ESP32 3.0+ (IDF 5.0+)

**To install correct version:**
1. Go to: Tools → Board → Boards Manager
2. Search for "esp32"
3. Select version **2.0.14** or **2.0.11**
4. Click Install

### Required Libraries

#### 1. Arduino_GFX Library
- **Name:** Arduino_GFX_Library (also called "GFX Library for Arduino")
- **Author:** moononournation
- **Version:** **1.3.7** or **1.4.7** ⚠️ **IMPORTANT: NOT newer versions!**
- **Installation:** Library Manager → Search "Arduino_GFX" → Select version **1.3.7**
- **GitHub:** https://github.com/moononournation/Arduino_GFX
- **Usage:** Display graphics on T-Display-S3
- **⚠️ WARNING:** Versions 1.4.8+ are incompatible with ESP32 2.0.14!

#### 2. WiFi (Built-in)
- **Included with:** ESP32 board package
- **No separate installation needed**
- **Usage:** WiFi connectivity

#### 3. HTTPClient (Built-in)
- **Included with:** ESP32 board package
- **No separate installation needed**
- **Usage:** HTTP communication (not required for this project)

### Display Configuration

**T-Display-S3 Display Specs:**
- Controller: ST7789
- Resolution: 320x170 pixels (landscape in rotation 3)
- Interface: 8-bit parallel
- Pin Configuration:
  - PWD: GPIO 15
  - Backlight: GPIO 38
  - DC: GPIO 7
  - CS: GPIO 6
  - WR: GPIO 8
  - RD: GPIO 9
  - Data: GPIO 39-42, 45-48
  - RST: GPIO 5

**Display Rotation:**
- `0` = Portrait
- `1` = Landscape 90°
- `2` = Portrait inverted 180°
- `3` = Landscape inverted 270° ✅ **Recommended**

### Button Configuration

**T-Display-S3 Button:**
- Pin: **GPIO 14** (most common)
- Alternative: GPIO 0 (some variants)
- Configuration: INPUT_PULLUP
- Active: LOW (pressed = LOW, released = HIGH)

### Serial Monitor Settings

**Baud Rate:** 115200
- This is critical - wrong baud rate = no output or garbage

**To view logs:**
1. Open Serial Monitor (Ctrl+Shift+M)
2. Set baud to **115200** (bottom right)
3. Select correct COM port
4. Press RESET button on ESP32

### WiFi Configuration

**Supported:** 2.4GHz only (ESP32 does not support 5GHz)

**Current settings:**
- SSID: `FamiliaSanchez.`
- Password: `F98movistar*`

### Avalon Q Miner Configuration

**Target Device:** Avalon Q Cryptocurrency Miner
- IP Address: **192.168.1.51**
- Port: **4028** (CGMiner API standard port)
- Protocol: TCP socket communication
- Current State: Standby mode

**API Commands:**
- Get version: `{"command":"version"}`
- Get stats: `{"command":"stats"}`
- Set frequency (ECO): `{"command":"ascset","parameter":"0,freq,600"}`

## Installation Steps

### 1. Install Arduino IDE
Download from: https://www.arduino.cc/en/software

### 2. Add ESP32 Board Support
1. File → Preferences
2. Additional Board Manager URLs: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. Tools → Board → Boards Manager
4. Search "esp32"
5. Install **ESP32 by Espressif Systems version 2.0.14**

### 3. Install Arduino_GFX Library (Version 1.3.7)
1. Sketch → Include Library → Manage Libraries
2. Search "Arduino_GFX"
3. Click on **"GFX Library for Arduino" by moononournation**
4. **Select version 1.3.7** from the dropdown (NOT latest!)
5. Click **Install**
6. Restart Arduino IDE

### 4. Select Board
1. Tools → Board → ESP32 Arduino → **ESP32S3 Dev Module**
2. Or: **LilyGO T-Display-S3** (if available)

### 5. Upload Code
1. Connect ESP32 via USB
2. Select correct COM port (Tools → Port)
3. Click Upload button (→)
4. Wait for "Done uploading"

## Troubleshooting

### No Serial Output
- ✅ Check baud rate is **115200**
- ✅ Try different COM ports
- ✅ Use USB data cable (not charge-only)
- ✅ Press RESET button after opening Serial Monitor

### Display Not Working
- ✅ Verify rotation setting (try 0, 1, 2, 3)
- ✅ Check backlight pin is HIGH (GPIO 38)
- ✅ Verify PWD pin is HIGH (GPIO 15)

### WiFi Not Connecting
- ✅ Check SSID and password
- ✅ Ensure 2.4GHz network (not 5GHz)
- ✅ Check signal strength

### Miner Not Responding
- ✅ Verify IP: `ping 192.168.1.51`
- ✅ Check miner is powered on
- ✅ Ensure API is enabled on miner
- ✅ Verify both devices on same network

## Version History

**2026-01-20:** Initial working configuration
- ESP32 board package: 2.0.14
- Arduino_GFX: Latest
- Confirmed working with LilyGO T-Display-S3
- Avalon Q control tested
