# Tasmota ESP12F 2CH 30A Relay Board - Complete Setup

## Board Info
- **Model:** ESP12F_Relay_30A X2_V1.1 (30312F30211)
- **Chip:** ESP8266EX
- **Flash:** 4096 KB
- **Tasmota Version:** 15.4.0.3
- **Tasmota Firmware:** tasmota (ESP8266 version)
- **MAC:** 48:3F:DA:60:49:91
- **Hostname:** tasmota-604991-2449
- **IP Address:** 192.168.1.77
- **Relay Rating:** 30A max per relay
- **Power Input:** 7-28 VDC or 5 VDC

### Board Schematic
- See: [T-Display-S3-Schema.html](T-Display-S3-Schema.html) for reference
- Schematic: https://es.scribd.com/document/801016873/ESP12F-Relay-30A-X2-V1-2024-10-04-00-27-16

---

## GPIO Mapping (from schematic)

| Pin | GPIO | Function | Hardware Connection |
|-----|------|----------|-------------------|
| RY1 | GPIO16 | Relay 1 | → R8 (1K) → Q1 (J3Y NPN) → K1 (30A Relay) |
| RY2 | GPIO14 | Relay 2 | → R15 (1K) → Q2 (J3Y NPN) → K2 (30A Relay) |
| S2 | GPIO0 | Button | Onboard push button |
| D14 | GPIO5 | Blue LED | Blue indicator LED |
| D1 | - | Red LED | Power indicator (always on) |

## ⚠️ Jumper Caps Required!
- **RY1 ↔ IO16** — short-circuit jumper cap must be installed
- **RY2 ↔ IO14** — short-circuit jumper cap must be installed
- Without jumpers, relays will NOT respond to any GPIO command

## Power Supply
- **VCC 7-28V** → LM2596S-5.0 regulator → 5V (powers ESP + relays)
- **5V pin** → Direct 5V input (alternative)
- **3.3V** → AMS1117-3.3 regulator (from 5V rail, powers ESP-12F)

---

## Complete Configuration Commands

### Step 1: Apply Template
```
Backlog Template {"NAME":"ESP12F 2CH 30A","GPIO":[0,0,0,0,0,0,0,0,0,0,225,0,224,0],"FLAG":0,"BASE":18}; Module 0
```

**Template breakdown:**
```
Position  0 (GPIO0)  = 0    (None - could set to 32 for Button1)
Position  1 (GPIO1)  = 0    (None - TX, used for serial)
Position  2 (GPIO2)  = 0    (None)
Position  3 (GPIO3)  = 0    (None - RX, used for serial)
Position  4 (GPIO4)  = 0    (None)
Position  5 (GPIO5)  = 0    (None - Blue LED available here)
Position  6 (GPIO9)  = 0    (None - flash pin)
Position  7 (GPIO10) = 0    (None - flash pin)
Position  8 (GPIO12) = 0    (None)
Position  9 (GPIO13) = 0    (None)
Position 10 (GPIO14) = 225  (Relay2)
Position 11 (GPIO15) = 0    (None)
Position 12 (GPIO16) = 224  (Relay1)
Position 13 (ADC0)   = 0    (None)
```

**GPIO values (Tasmota v9.1+ numbering):**
- `224` = Relay1 (active HIGH)
- `225` = Relay2 (active HIGH)
- `32` = Button1
- `320` = LED1 inverted
- `0` = None

### Step 2: Set Admin Password
```
WebPassword Fourier98
```

### Step 3: Set Device Name (optional)
```
DeviceName ESP12F Relay Board
FriendlyName1 BitAxe Power
FriendlyName2 Octaxe Power
```

### Step 4: Disable Serial Log (recommended)
```
SerialLog 0
```

### Step 5: Set Timezone (Colombia UTC-5)
```
Timezone -5
```

---

## Test Commands

### Basic Relay Control
```
Power1 ON       → Relay 1 ON (click!)
Power1 OFF      → Relay 1 OFF
Power1 TOGGLE   → Toggle Relay 1

Power2 ON       → Relay 2 ON (click!)
Power2 OFF      → Relay 2 OFF
Power2 TOGGLE   → Toggle Relay 2
```

### Check Status
```
Status 0        → Full device status (JSON)
Status          → Basic status
Status 5        → Network status
Status 11       → GPIO/Template status
```

### Check Template
```
Template        → Shows current template config
Module          → Shows current module
Gpio            → Shows GPIO assignments (ESP32 only)
```

---

## HTTP API Commands

### With Basic Auth (admin/Fourier98):

**Toggle Relay 1:**
```
http://192.168.1.77/cm?cmnd=Power1%20TOGGLE
```

**Turn Relay 1 ON:**
```
http://192.168.1.77/cm?cmnd=Power1%20ON
```

**Turn Relay 1 OFF:**
```
http://192.168.1.77/cm?cmnd=Power1%20OFF
```

**Turn Relay 2 ON:**
```
http://192.168.1.77/cm?cmnd=Power2%20ON
```

**Turn Relay 2 OFF:**
```
http://192.168.1.77/cm?cmnd=Power2%20OFF
```

**Get Full Status:**
```
http://192.168.1.77/cm?cmnd=Status%200
```

**With credentials in URL:**
```
http://admin:Fourier98@192.168.1.77/cm?cmnd=Power1%20ON
```

### Using curl:
```bash
# Relay 1 ON
curl -u admin:Fourier98 "http://192.168.1.77/cm?cmnd=Power1%20ON"

# Relay 2 TOGGLE
curl -u admin:Fourier98 "http://192.168.1.77/cm?cmnd=Power2%20TOGGLE"

# Get Status
curl -u admin:Fourier98 "http://192.168.1.77/cm?cmnd=Status%200"
```

---

## Optional: Add Button and LED

To also use the onboard button (GPIO0) and blue LED (GPIO5):
```
Backlog Template {"NAME":"ESP12F 2CH 30A","GPIO":[32,0,0,0,0,320,0,0,0,0,225,0,224,0],"FLAG":0,"BASE":18}; Module 0
```

Changes:
- Position 0 (GPIO0) = 32 (Button1 - toggles Relay1)
- Position 5 (GPIO5) = 320 (LED1 inverted - status indicator)

---

## Factory Reset (if needed)
```
Reset 1         → Full factory reset (erases WiFi, all settings)
Restart 1       → Simple restart (keeps settings)
```

## OTA Update
```
Upgrade 1       → Update firmware over the air
```

---

## Troubleshooting

### Power commands return "Error":
- Template not applied. Re-run the Template command and `Module 0`
- Check that POWER1/POWER2 appear in `Status 0` response

### Relays don't click:
1. Check jumper caps (RY1↔IO16, RY2↔IO14)
2. Check power supply (need 5V to relay coils)
3. Verify template: `Template` should show values 224 and 225

### HTTP 401 Unauthorized:
- Password is set. Use Basic Auth: admin / Fourier98
- To remove password: `WebPassword 0`

### Can't connect to WiFi:
- Reset and reconfigure: `Reset 1`
- Board creates AP: tasmota-XXXX (192.168.4.1)

---

## Comparison: Both Relay Boards

| Feature | ESP12F 2CH 30A (this) | ESP32 Relay x2 |
|---------|----------------------|----------------|
| Chip | ESP8266EX | ESP32-WROOM-32E |
| Relay Rating | **30A** | 10A |
| Relay GPIOs | GPIO16, GPIO14 | GPIO16, GPIO17 |
| LED GPIO | GPIO5 (Blue) | GPIO23 |
| Button GPIO | GPIO0 | GPIO0 |
| Jumper Caps | **Required** (RY1↔IO16, RY2↔IO14) | Not needed |
| Power Input | 7-28V or 5V | 7-30V or 5V |
| Voltage Regulator | LM2596S-5.0 | Onboard |
| Tasmota Config | Template command ✅ | Manual GPIO in web UI ✅ |
| Tasmota Firmware | tasmota (ESP8266) | tasmota32 (ESP32) |
| IP Address | **192.168.1.77** | 192.168.1.78 |
| Flash Size | 4096 KB | 4 MB + UFS |
| USB | No USB, flash via serial | No USB, 6-pin header |
| Relay Driver | NPN transistor (J3Y) | Direct GPIO |

---

## Device Details
```
Program Version:     15.4.0.3 (6191473-tasmota)
Build Date:          2026-06-03T15:22:24
Core/SDK:            2.7.8/2.2.2-dev(38a443e)
ESP Chip Id:         6310289 (ESP8266EX)
Flash Chip Id:       0x164068 (DOUT)
Flash Size:          4096 KB
Program Flash Size:  1024 KB
Program Size:        650 KB
Free Program Space:  352 KB
Free Memory:         25.2 KB
WiFi SSID:           FamiliaSanchez.
Gateway:             192.168.1.1
Subnet:              255.255.255.0
DNS1:                200.21.200.80
DNS2:                200.21.200.10
```
