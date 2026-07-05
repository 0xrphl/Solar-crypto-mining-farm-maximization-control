# Tasmota ESP32 2X Relay Board - Complete Setup

## Board Info
- **Model:** ESP32 Relay x2
- **Chip:** ESP32-WROOM-32E (ESP32-D0WD-V3 v3.1)
- **Tasmota Version:** 15.4.0.3 (tasmota32)
- **MAC:** 68:09:47:58:F6:84
- **Hostname:** tasmota-58F684-5764
- **IP Address:** 192.168.1.78
- **Relay Rating:** 10A max per relay (COM/NO/NC terminals)
- **Power Input:** 7-30 VDC or 5 VDC (separate terminals)

### Product Image
![ESP32 Relay x2](Hd3380ac938e94c9887fb39296f90fb21E.avif)

---

## GPIO Mapping

| GPIO | Function | Comment |
|------|----------|---------|
| GPIO0 | Button | Onboard push button (hold LOW for flash mode) |
| GPIO16 | Relay 1 | Outermost relay (COM/NO/NC) |
| GPIO17 | Relay 2 | Inner relay (COM/NO/NC) |
| GPIO23 | LED | Onboard status LED |

**Note:** The board has headers for every GPIO pin on the ESP32, labeled on the reverse side.

---

## Power Supply
- **7-30 VDC** → Main power input terminals
- **5 VDC** → Alternative power input (separate terminals)
- Both power the ESP32 and relay coils
- **⚠️ Must have power connected for relays to click!**

---

## USB Programming Header (6-pin)

**⚠️ NOT standard FTDI pinout!**

| CP2102 Pin | → | Board Header |
|-----------|---|-------------|
| GND | → | GND |
| +5V | → | +5V |
| TXD | → | RXD |
| RXD | → | TXD |
| - | - | GND (extra) |
| - | - | IO0 (hold LOW for flash) |

### Enter Flash Mode:
1. Hold **IO0** button (or connect IO0 to GND)
2. Press **EN** button (reset)
3. Release IO0
4. Flash with esptool or Tasmota Web Installer

---

## Tasmota Installation

### Option 1: Web Installer (Easiest)
1. Open Chrome/Edge: **https://tasmota.github.io/install/**
2. Select: **Tasmota32** (the plain one, NOT S2/S3/C3/bluetooth/display)
3. Click **CONNECT** → select COM port
4. Check **"Erase device"** ✅
5. Click **INSTALL**
6. After flashing → configure WiFi: **FamiliaSanchez.** / **Movistar0898***

### Option 2: esptool.py
```bash
pip install esptool
esptool.py --port COM3 --baud 460800 write_flash 0x0 tasmota32.factory.bin
```
Download firmware: https://ota.tasmota.com/tasmota32/release/

---

## ⚡ Tasmota Configuration (WORKING!)

### ⚠️ IMPORTANT: Use Manual GPIO Configuration (NOT Template!)

The ESP32 template GPIO positions **do NOT map 1:1 to GPIO numbers**. Using the web interface to manually set GPIOs is the reliable method.

### Method: Configure Module (Web Interface)

1. Go to **http://192.168.1.78**
2. Click **Configuration** → **Configure Module**
3. Set **Module Type** to: `Generic (0)`
4. Find **GPIO16** in the dropdown → set to **Relay 1** (224)
5. Find **GPIO17** in the dropdown → set to **Relay 2** (225)
6. Find **GPIO23** in the dropdown → set to **LED 1i** (320)
7. Find **GPIO0** in the dropdown → set to **Button 1** (32)
8. Click **Save** → board reboots

### After reboot, verify in Console:
```
GPIO16 → Relay1 (224)
GPIO17 → Relay2 (225)
GPIO23 → LED1i (320)
GPIO0  → Button1 (32)
```

### Alternative: Direct GPIO Commands (Console)
```
Backlog Module 0; GPIO16 224; GPIO17 225; GPIO23 320; GPIO0 32
```

### ❌ Template Method (DOES NOT WORK RELIABLY on ESP32!)
The following template was tried but GPIOs did NOT map correctly:
```
# DO NOT USE - GPIO positions are wrong for ESP32
Template {"NAME":"ESP32 Relay x2","GPIO":[32,0,0,0,0,0,0,0,0,0,224,225,0,0,0,0,0,320,...],...}
```
**Use the manual Configure Module method instead!**

---

## Additional Configuration

### Set Admin Password
```
WebPassword Fourier98
```

### Set Device Names (optional)
```
DeviceName ESP32 Relay Board
FriendlyName1 Relay 1 Power
FriendlyName2 Relay 2 Power
```

### Set Timezone (Colombia UTC-5)
```
Timezone -5
```

---

## Test Commands

### Basic Relay Control
```
Power1 ON       → Relay 1 ON (click! ✅)
Power1 OFF      → Relay 1 OFF (click! ✅)
Power1 TOGGLE   → Toggle Relay 1

Power2 ON       → Relay 2 ON (click! ✅)
Power2 OFF      → Relay 2 OFF (click! ✅)
Power2 TOGGLE   → Toggle Relay 2
```

### LED Control
```
Power3 ON       → LED ON
Power3 OFF      → LED OFF
```

### Check Status
```
Status 0        → Full device status (JSON)
Status 11       → GPIO configuration
```

---

## HTTP API Commands

### With Basic Auth (admin/Fourier98):

**Relay 1 ON:**
```
http://192.168.1.78/cm?cmnd=Power1%20ON
```

**Relay 1 OFF:**
```
http://192.168.1.78/cm?cmnd=Power1%20OFF
```

**Relay 2 ON:**
```
http://192.168.1.78/cm?cmnd=Power2%20ON
```

**Relay 2 OFF:**
```
http://192.168.1.78/cm?cmnd=Power2%20OFF
```

**Toggle Relay 1:**
```
http://192.168.1.78/cm?cmnd=Power1%20TOGGLE
```

**Get Full Status:**
```
http://192.168.1.78/cm?cmnd=Status%200
```

**With credentials in URL:**
```
http://admin:Fourier98@192.168.1.78/cm?cmnd=Power1%20ON
```

### Using curl:
```bash
# Relay 1 ON
curl -u admin:Fourier98 "http://192.168.1.78/cm?cmnd=Power1%20ON"

# Relay 2 TOGGLE
curl -u admin:Fourier98 "http://192.168.1.78/cm?cmnd=Power2%20TOGGLE"

# Get Status
curl -u admin:Fourier98 "http://192.168.1.78/cm?cmnd=Status%200"
```

---

## Device Details (from Tasmota Console)
```
Hardware:            ESP32-D0WD-V3 v3.1
Program Version:     15.4.0.3 (6191473-tasmota32)
Build Date:          2026-06-03T15:28:38
Berry Runtime:       RAM used 3766 bytes
Hostname:            tasmota-58F684-5764
IP Address:          192.168.1.78
WiFi SSID:           FamiliaSanchez.
WiFi RSSI:           90% (-55 dBm)
WiFi Mode:           HT20, Channel 1
Flash Size:          UFS 304 kB free
Heap:                117 kB
```

---

## Troubleshooting

### Relays don't click (software shows ON/OFF):
1. **Use Configure Module** web interface, NOT template commands
2. Verify GPIO16=Relay1 and GPIO17=Relay2 in Configuration
3. Check power supply: 5V or 7-30V must be connected
4. Try pressing the physical button on the board

### LED doesn't light:
- Set GPIO23 to **LED 1i** (inverted, value 320)
- NOT LED 1 (value 288) — the LED is active LOW

### Can't flash:
1. Connect CP2102: GND, 5V, TX→RX, RX→TX
2. Hold IO0 button → press EN → release IO0
3. Flash with Tasmota Web Installer in Chrome

### HTTP 401 Unauthorized:
- Password set. Use: admin / Fourier98
- Remove password: `WebPassword 0`

### Factory Reset:
```
Reset 1       → Full factory reset
Restart 1     → Simple restart (keeps settings)
```

---

## Comparison: ESP12F vs ESP32 Relay Boards

| Feature | ESP12F 2CH 30A | ESP32 Relay x2 |
|---------|---------------|----------------|
| Chip | ESP8266EX | ESP32-WROOM-32E |
| Relay Rating | 30A | 10A |
| Relay GPIOs | GPIO16, GPIO14 | GPIO16, GPIO17 |
| LED GPIO | GPIO5 | GPIO23 |
| Button GPIO | GPIO0 | GPIO0 |
| Jumper Caps | Required (RY1↔IO16, RY2↔IO14) | Not needed |
| Power Input | 7-28V or 5V | 7-30V or 5V |
| Tasmota Config | Template works ✅ | Manual GPIO only ✅ |
| IP Address | 192.168.1.77 | 192.168.1.78 |
| USB | No USB, flash via serial | No USB, 6-pin header |
