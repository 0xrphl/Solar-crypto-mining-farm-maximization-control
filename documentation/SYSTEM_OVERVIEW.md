# ⚡ Solar Mining Control System — Complete Overview

## Architecture

Autonomous solar-powered Bitcoin mining system. An ESP32-S3 monitors real-time energy
via a Refoss EM06P 6-channel energy monitor and automatically scales mining operations
(0–2001W, 16 profiles) to match available solar surplus — mining ONLY with free solar energy.

![System Architecture](../diagrams/system_architecture.svg)

## Hardware

| Device | Model | IP | Role | Power |
|--------|-------|-----|------|-------|
| **Controller** | [LilyGO T-Display-S3](https://www.aliexpress.us/item/3256804310228562.html) | .26 | Brain, WebUI, decisions | 2W |
| **Energy Monitor** | [Refoss EM06P](https://www.aliexpress.us/item/3256809503697509.html) | .82 | 6-ch CT clamp measurement | 2W |
| **Relay Board** | [ESP32 2CH Relay](https://www.aliexpress.us/item/3256804243304840.html) | .78 | Tasmota power switching | 3W |
| **Miner** | [BitAxe Gamma 601](https://www.aliexpress.us/item/3256808067170426.html) | .21 | 1.5 TH/s SHA-256 | 21W |
| **Miner** | [NerdQAxe+](https://www.aliexpress.us/item/3256808693446062.html) | .28 | 2.5 TH/s SHA-256 | 60W |
| **Miner** | [Nerd Octaxe](https://www.aliexpress.us/item/3256808957009912.html) | .37 | 10.7 TH/s SHA-256 | 180W |
| **Miner** | [Avalon Q](https://nhash.net/products/canaan-avalon-q-90th-1674w-bitcoin-btc-miner-free-shipping) | .51 | 52-90 TH/s SHA-256 | 800-1720W |
| **Cloud DB** | Supabase PostgreSQL | cloud | Energy logging, transitions | — |

## Relay Mapping

| Relay | Miners | Total Watts | Control |
|-------|--------|-------------|---------|
| **R1** | BitAxe (21W) + Nerdaxe (60W) | **81W** | Tasmota Power1 ON/OFF |
| **R2** | Octaxe | **180W** | Tasmota Power2 ON/OFF |
| **API** | Avalon Q | **800/1600/1720W** | CGMiner TCP:4028 (no relay) |

## CT Clamp Channel Mapping (Refoss EM06P)

![Channel Mapping](../diagrams/channel_mapping.svg)

Bi-phase electrical system. All three quantities are **directly measured**:

| Clamp | Channel | Measures | Formula Role |
|-------|---------|----------|-------------|
| **A1** | ch1/em:1 | ☀️ Solar Phase A | `Solar = A1 + |B2|` |
| **B2** | ch5/em:5 | ☀️ Solar Phase B | |
| **A2** | ch4/em:4 | ⚡ Grid Phase A | `Grid = A2 + C2` (neg=export) |
| **C2** | ch6/em:6 | ⚡ Grid Phase B | |
| **B1** | ch2/em:2 | 🏠 House Phase B | `House = |B1| + |C1|` |
| **C1** | ch3/em:3 | 🚿 Shower | |

> **Note:** Refoss EM06P reports **Active Power (W)**, not Apparent (VA). PF reported separately.

## Data Flow

```
Refoss EMO6P ──HTTP GET /rpc/Refoss.Status.Get──▶ ESP32-S3
   (every 30s)                                      │
                                                    │ buffers up to 12 samples
                                              every 5 minutes
                                                    │
                                    ┌───────────────┼───────────────┐
                                    ▼               ▼               ▼
                            verifyMinerStates  runMiningDecision  pushToSupabase
                            (power outage       │               │
                             recovery)   ┌──────┴──────┐        │
                                         ▼              ▼        ▼
                                 Tasmota Relays   Avalon Q    Supabase
                                 (HTTP cmnd)     (TCP 4028)  (HTTPS REST)
```

---

## Mining Decision Engine

**Rule:** Mine ONLY with solar surplus. Never import from grid for mining.

### The 16 Mining Profiles

![Mining Profiles](../diagrams/mining_profiles.svg)

| # | Profile | R1 (BN 81W) | R2 (Oct 180W) | Avalon Q | Total W |
|---|---------|:-----------:|:-------------:|:--------:|--------:|
| 0 | OFF | ❌ | ❌ | off | **0** |
| 1 | BN | ✅ | ❌ | off | **81** |
| 2 | OCT | ❌ | ✅ | off | **180** |
| 3 | BN+OCT | ✅ | ✅ | off | **261** |
| 4 | AV_LO | ❌ | ❌ | low 800W | **800** |
| 5 | AV_LO+BN | ✅ | ❌ | low 800W | **881** |
| 6 | AV_LO+OCT | ❌ | ✅ | low 800W | **980** |
| 7 | AV_LO+BN+OCT | ✅ | ✅ | low 800W | **1061** |
| 8 | AV_MD | ❌ | ❌ | mid 1600W | **1600** |
| 9 | AV_MD+BN | ✅ | ❌ | mid 1600W | **1681** |
| 10 | AV_HI | ❌ | ❌ | high 1720W | **1720** |
| 11 | AV_MD+OCT | ❌ | ✅ | mid 1600W | **1780** |
| 12 | AV_HI+BN | ✅ | ❌ | high 1720W | **1801** |
| 13 | AV_MD+BN+OCT | ✅ | ✅ | mid 1600W | **1861** |
| 14 | AV_HI+OCT | ❌ | ✅ | high 1720W | **1900** |
| 15 | AV_HI+BN+OCT | ✅ | ✅ | high 1720W | **2001** |

### Surplus Calculation

```
surplus = -avgGrid + currentMiningWatts
```

The grid reading already includes current mining. Adding `currentMiningWatts` reveals true available solar:

```
Example: Currently at S7 (1061W), Grid reads +200W (importing)
  surplus = -200 + 1061 = 861W → picks S4 (AV_LO 800W) ✅
  Without correction: surplus = -200 → turns all off ❌
```

### Decision Algorithm (every 5 minutes)

1. **Verify** actual device states match expected (power outage recovery)
2. **Average** all grid readings from 5-min window
3. **Calculate** `surplus = -avgGrid + profiles[currentState].totalW`
4. **Select** highest profile where `totalW ≤ surplus`
5. **Execute** relay + Avalon Q commands if state changed
6. **Log** transition to Supabase (old_id == new_id = correction)

### Example Day

```
 Time    Solar   Home    Grid    Surplus  Action
 06:00   0W      400W    +400W   0W       S0  OFF (no solar)
 08:00   200W    400W    +200W   200W     S2  OCT (180W)
 10:00   1200W   500W    -700W   961W     S6  AV_LO+OCT (980W)
 11:00   2000W   500W    -1500W  2480W    S15 AV_HI+BN+OCT (2001W)
 14:00   1000W   500W    -500W   1220W    S7  AV_LO+BN+OCT (1061W)
 18:00   0W      600W    +600W   0W       S0  OFF (sunset)
```

---

## Solar Array & Inverters

### Inverter Configuration

3× **Hoymiles HMS-2000-4T** microinverters, each with 4 MPPT inputs.
Total: **12 solar panels**, **7,740W peak capacity**.

| Inverter | Input | Panels | Config | Power | Voc | Isc | Imppt |
|----------|-------|--------|--------|-------|-----|-----|-------|
| **HMS-2000-4T #1** | I1M1 | 1 panel | 1P/1S | 645W | 58.3V | 15.21A | 14.51A |
| | I1M2 | 1 panel | 1P/1S | 645W | 58.3V | 15.21A | 14.51A |
| | I1M3 | 1 panel | 1P/1S | 645W | 58.3V | 15.21A | 14.51A |
| | I1M4 | 1 panel | 1P/1S | 645W | 58.3V | 15.21A | 14.51A |
| **HMS-2000-4T #2** | I2M1 | 1 panel | 1P/1S | 645W | 58.3V | 15.21A | 14.51A |
| | I2M2 | 1 panel | 1P/1S | 645W | 58.3V | 15.21A | 14.51A |
| | I2M3 | 1 panel | 1P/1S | 645W | 58.3V | 15.21A | 14.51A |
| | I2M4 | 1 panel | 1P/1S | 645W | 58.3V | 15.21A | 14.51A |
| **HMS-2000-4T #3** | I3M1 | 1 panel | 1P/1S | 645W | 58.3V | 15.21A | 14.51A |
| | I3M2 | 1 panel | 1P/1S | 645W | 58.3V | 15.21A | 14.51A |
| | I3M3 | 1 panel | 1P/1S | 645W | 58.3V | 15.21A | 14.51A |
| | I3M4 | 1 panel | 1P/1S | 645W | 58.3V | 15.21A | 14.51A |

### Panel Specifications

| Spec | Value |
|------|-------|
| Panel Power | 645W per panel |
| Voc (Open Circuit Voltage) | 58.3V |
| Isc (Short Circuit Current) | 15.21A |
| Imppt (Max Power Current) | 14.51A |
| Total Panels | 12 |
| Total Peak Capacity | **7,740W** |
| Inverter Total Capacity | 3 × 2,000W = **6,000W** (AC output limit) |

### System Summary

```
12 × 645W panels ──▶ 3 × Hoymiles HMS-2000-4T ──▶ Distribution Panel ──▶ Grid / House / Miners
     7,740Wp              6,000W AC max                  Bi-phase
```

> **Note:** Peak solar capacity (7,740W) exceeds inverter AC output limit (6,000W).
> This provides headroom for partial shading and non-ideal conditions.

---

## Parts List

### Mining Hardware

| Component | Specs | Power | Source |
|-----------|-------|-------|--------|
| [Canaan Avalon Q](https://nhash.net/products/canaan-avalon-q-90th-1674w-bitcoin-btc-miner-free-shipping) | 90 TH/s SHA-256 | 800/1600/1720W | NHASH |
| [BitAxe Gamma 601](https://www.aliexpress.us/item/3256808067170426.html) | 1.5 TH/s BM1366 | 25W (21W measured) | AliExpress |
| [NerdQAxe+](https://www.aliexpress.us/item/3256808693446062.html) | 2.5 TH/s | 60W | AliExpress |
| [Nerd Octaxe](https://www.aliexpress.us/item/3256808957009912.html) | 8-Chip BM1370 | 180W | AliExpress |

### Control Electronics

| Component | Role |
|-----------|------|
| [LILYGO T-Display-S3](https://www.aliexpress.us/item/3256804310228562.html) | Main controller, WebUI, decisions |
| [ESP32-WROOM 2CH Relay](https://www.aliexpress.us/item/3256804243304840.html) | Tasmota relay board (R1/R2) |
| [ESP8266 2CH 30A Relay](https://www.aliexpress.us/item/3256802365448988.html) | Backup relay board |
| [Refoss EM06P](https://www.aliexpress.us/item/3256809503697509.html) | 6-Channel WiFi energy monitor |
| [USB-TTL Adapters](https://www.aliexpress.us/item/3256806910315665.html) | PL2303/CP2102/CH340G for programming |

### Power Supplies

| Component | Output |
|-----------|--------|
| [DC 12V Switching PSU 100W](https://www.aliexpress.us/item/3256807208787904.html) | Powers Octaxe |
| DC 5V/12V Dual PSU | Powers BitAxe + NerdQAxe |
| [USB Wall Charger 5V 2A](https://www.aliexpress.us/item/3256808314505260.html) | Powers ESP32 controller |

### Tools & Wiring

| Component | Purpose |
|-----------|---------|
| [O-type Lugs Terminals](https://www.aliexpress.us/item/3256806062864141.html) | CT clamp wire termination |
| [Wire Crimping Kit AWG 23-13](https://www.aliexpress.us/item/3256810332001270.html) | Crimping connectors |
| [Wire Stripping Pliers](https://www.aliexpress.us/item/3256808741160255.html) | Wire preparation |
| [Cooling Fan Guards](https://www.aliexpress.us/item/3256805281155490.html) | Miner ventilation |
| [Screw Set M3-6](https://www.aliexpress.us/item/3256807755286435.html) | Mounting hardware |

All photos in `Miner_solar_cluster_imgs/` directory.
