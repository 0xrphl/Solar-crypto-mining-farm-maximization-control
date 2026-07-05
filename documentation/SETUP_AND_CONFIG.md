# 🔧 Setup & Configuration Guide

## Quick Start

1. Run `supabase_schema.sql` in [Supabase SQL Editor](https://supabase.com/dashboard)
2. Upload `MinerControl_WebUI_FINAL/MinerControl_WebUI_FINAL.ino` to ESP32-S3
3. Open `http://192.168.1.26` in browser
4. System auto-discovers Refoss, starts polling, begins decisions after 5 min

---

## Arduino IDE Setup

### Board Manager
- **Important:** Use **Arduino ESP32 version < 3.0**
- ✅ **Working:** 2.0.11 or 2.0.14
- ❌ **Not Working:** 3.0+ (IDF 5.0+)

**Install:**
1. File → Preferences → Additional Board Manager URLs:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Tools → Board → Boards Manager → search "esp32" → version **2.0.14**

### Board Settings
| Setting | Value |
|---------|-------|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| USB Mode | Hardware CDC and JTAG |
| Flash Size | 16MB |
| Partition Scheme | 16M Flash (3MB APP) |
| PSRAM | OPI PSRAM |
| Upload Speed | 921600 |

### Required Library
- **Arduino_GFX** by moononournation — version **1.3.7** (NOT newer!)
- Install: Sketch → Include Library → Manage Libraries → "Arduino_GFX" → select 1.3.7
- ⚠️ Versions 1.4.8+ incompatible with ESP32 2.0.14

### Upload
1. Connect ESP32-S3 via USB
2. Select correct COM port
3. Click Upload → "Done uploading"
4. Open Serial Monitor at 115200 baud

---

## T-Display-S3 Pin Configuration

| Pin | GPIO | Function |
|-----|------|----------|
| PWD | 15 | Display power |
| Backlight | 38 | Display backlight |
| DC | 7 | Display data/command |
| CS | 6 | Display chip select |
| WR | 8 | Display write |
| RD | 9 | Display read |
| Data | 39-42, 45-48 | 8-bit parallel data |
| RST | 5 | Display reset |
| Button | 14 | User button (INPUT_PULLUP) |

Display: ST7789, 320x170 pixels, rotation 3 (landscape inverted)

---

## Network Configuration

| Device | IP | Port | Protocol |
|--------|-----|------|----------|
| ESP32-S3 Controller | 192.168.1.26 | 80 | HTTP |
| Refoss EM06P | 192.168.1.82 | 80 | HTTP RPC |
| Tasmota Relay (active) | 192.168.1.78 | 80 | HTTP cmnd |
| Tasmota Relay (backup) | 192.168.1.77 | 80 | HTTP cmnd |
| BitAxe Rafa | 192.168.1.21 | 80 | HTTP API |
| NerdQAxe+ | 192.168.1.28 | 80 | HTTP API |
| Octaxe | 192.168.1.37 | 80 | HTTP API |
| Avalon Q | 192.168.1.51 | 4028 | TCP CGMiner |

WiFi: `FamiliaSanchez.` (2.4GHz only)

---

## Tasmota Relay Board Setup

### Active Board: ESP32-WROOM 2CH Relay (192.168.1.78)

| Spec | Value |
|------|-------|
| Chip | ESP32-WROOM-32E (ESP32-D0WD-V3 v3.1) |
| Tasmota | 15.4.0.3 (tasmota32) |
| MAC | 68:09:47:58:F6:84 |
| Relay Rating | 10A per relay |
| Power Input | 7-30 VDC or 5 VDC |

**GPIO Mapping:**
| GPIO | Function |
|------|----------|
| GPIO0 | Button |
| GPIO16 | Relay 1 (R1: BitAxe+Nerdaxe) |
| GPIO17 | Relay 2 (R2: Octaxe) |
| GPIO23 | LED |

**Configuration (use web interface, NOT template!):**
1. Go to `http://192.168.1.78` → Configuration → Configure Module
2. Module Type: `Generic (0)`
3. GPIO16 → Relay 1 (224), GPIO17 → Relay 2 (225)
4. GPIO23 → LED 1i (320), GPIO0 → Button 1 (32)

Or via console:
```
Backlog Module 0; GPIO16 224; GPIO17 225; GPIO23 320; GPIO0 32
WebPassword YOUR_TASMOTA_PASSWORD
Timezone -5
```

**⚠️ Template method does NOT work reliably on ESP32!**

### Backup Board: ESP12F 2CH 30A Relay (192.168.1.77)

| Spec | Value |
|------|-------|
| Chip | ESP8266EX |
| Relay Rating | 30A per relay |
| GPIO14 | Relay 2, GPIO16 | Relay 1 |
| ⚠️ Jumper caps required | RY1↔IO16, RY2↔IO14 |

**Configuration (Template works on ESP8266):**
```
Backlog Template {"NAME":"ESP12F 2CH 30A","GPIO":[0,0,0,0,0,0,0,0,0,0,225,0,224,0],"FLAG":0,"BASE":18}; Module 0
WebPassword YOUR_TASMOTA_PASSWORD
```

### Comparison

| Feature | ESP12F (backup .77) | ESP32 (active .78) |
|---------|---------------------|---------------------|
| Chip | ESP8266EX | ESP32-WROOM-32E |
| Relay Rating | **30A** | 10A |
| Relay GPIOs | GPIO16, GPIO14 | GPIO16, GPIO17 |
| Jumper Caps | **Required** | Not needed |
| Tasmota Config | Template ✅ | Manual GPIO ✅ |

### HTTP API (both boards)
```bash
# Relay ON/OFF
curl -u admin:YOUR_PASSWORD "http://192.168.1.78/cm?cmnd=Power1%20ON"
curl -u admin:YOUR_PASSWORD "http://192.168.1.78/cm?cmnd=Power2%20OFF"
curl -u admin:YOUR_PASSWORD "http://192.168.1.78/cm?cmnd=Status%200"
```

### USB Programming (ESP32 board, 6-pin header)
1. Connect CP2102: GND, 5V, TX→RX, RX→TX
2. Hold IO0 → press EN → release IO0
3. Flash with Tasmota Web Installer: https://tasmota.github.io/install/

---

## Credentials (in sketch)

| Service | Value |
|---------|-------|
| WiFi | `YOUR_WIFI_SSID` / `YOUR_WIFI_PASSWORD` |
| Supabase URL | `https://YOUR_PROJECT.supabase.co` |
| Supabase Key | `YOUR_SUPABASE_ANON_KEY` |
| Tasmota | admin / `YOUR_TASMOTA_PASSWORD` |
| Refoss EM06P | Auto-discovered, auth: admin / `YOUR_REFOSS_PASSWORD` |

---

## Troubleshooting

### No Serial Output
- Check baud rate: **115200**
- Use USB data cable (not charge-only)
- Press RESET button after opening Serial Monitor

### Display Not Working
- Verify backlight pin HIGH (GPIO 38) and PWD pin HIGH (GPIO 15)
- Try rotation values 0-3

### WiFi Not Connecting
- Must be 2.4GHz (ESP32 doesn't support 5GHz)
- Check SSID/password match exactly

### Relay Not Clicking
- ESP32 board: Use Configure Module web interface, NOT template
- ESP8266 board: Check jumper caps (RY1↔IO16, RY2↔IO14)
- Check power supply connected (5V or 7-30V)

### Miner Not Responding
- Verify IP: `ping 192.168.1.xx`
- Ensure same WiFi network
- Check miner API is enabled

### HTTP 401 from Tasmota
- Password is set. Use Basic Auth: admin / YOUR_TASMOTA_PASSWORD
- To reset: `WebPassword 0` in Tasmota Console
