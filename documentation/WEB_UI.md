# 🌐 Web UI Guide

## Access

Open `http://192.168.1.26` in any browser on the same WiFi network.

## Interface Layout

```
⚡ Solar Mining Control
ESP32 @ 192.168.1.26

┌─────────────────────────────────────┐
│ 🔌 Energy Monitor (EMO6P)           │
│ Solar: 1234W  Grid: -567W  Home: 890W│
│ [A1_Solar] [B1_House] [C1_Shower]    │
│ [A2_Grid]  [B2_Solar] [C2_Grid]      │
│ 8 samples | Next push: 2:30          │
└─────────────────────────────────────┘

┌─────────────────────────────────────┐
│ 🔌 R1 — BitAxe + Nerdaxe (81W)  ON │
│ [✅ ON] [❌ OFF]                     │
│ ┌ BitAxe Rafa 21W (.21)            │
│ │ [📊 Status] [🔄 Restart]         │
│ ┌ Nerdaxe 60W (.28)                │
│ │ [📊 Status] [🔄 Restart]         │
└─────────────────────────────────────┘

┌─────────────────────────────────────┐
│ 🔌 R2 — Octaxe (180W)          OFF │
│ [✅ ON] [❌ OFF]                     │
│ ┌ Octaxe 180W (.37)                │
│ │ [📊 Status] [🔄 Restart]         │
└─────────────────────────────────────┘

┌─────────────────────────────────────┐
│ 🔌 Avalon Q (API Only)              │
│ 192.168.1.51:4028 • CGMiner         │
│ [📊 Get Status]                     │
│ [💤 Sleep] [☀ Wake]                 │
│ [🐌 Low] [⚡ Mid] [🚀 High]        │
│ [🔄 Reboot]                         │
└─────────────────────────────────────┘

┌─────────────────────────────────────┐
│ 📊 Supabase Cloud DB                │
│ [🔄 Sync Now]                       │
└─────────────────────────────────────┘
```

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Full web UI HTML |
| `/energy` | GET | JSON: solar, grid, home, channels, mining state |
| `/status?miner=N` | GET | JSON: miner hashrate, temp, power |
| `/restart?miner=N` | GET | Restart miner N |
| `/relay?relay=N&action=ON/OFF` | GET | Control relay |
| `/relaystatus` | GET | JSON: relay states, uptime, RSSI |
| `/avalon?miner=3&cmd=X` | GET | Avalon Q commands |
| `/supabase` | GET | Force push to Supabase |

### Avalon Q Commands
`standby`, `wakeup`, `low`, `mid`, `high`, `reboot`, `summary`

## Auto-Refresh

| Data | Interval |
|------|----------|
| Energy readings | 10 seconds |
| Relay status | 15 seconds |
| Mining decisions | 5 minutes (server-side) |

## ESP32 Display (T-Display-S3)

The 1.9" LCD shows:
- Line 1: "Solar Miner" title
- Line 2: IP address
- Line 3: Solar/Grid/Home watts (live)
- Line 4: Relay states + mining profile
- Line 5: Last action
- Line 6: Supabase sync status
