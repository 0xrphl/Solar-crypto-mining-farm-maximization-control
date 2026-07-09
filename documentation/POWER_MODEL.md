# ⚡ 2-Phase Power Model — Phasorial Analysis & Calculations

> **Complete mathematical model for the Refoss EM06P 6-channel energy monitor**
> in a 2-phase (bi-phase) residential solar installation.

---

## 📐 Circuit Topology

The house is fed by a **2-phase (split-phase / bi-phase)** service from the utility.
Solar microinverters inject on both phases. The Refoss EM06P has 6 CT clamp channels
distributed across both phases to measure solar generation, grid exchange, and house consumption.

```
                    ┌─────────────────────────────────────────────┐
                    │           DISTRIBUTION PANEL                │
                    │                                             │
  ═══ PHASE 1 (A) ═╪═════════════════════════════════════════════╪═══
        │           │                                             │
        │     ┌─────┴─────┐                                      │
        │     │  A1 (ch1)  │◄── CT Clamp: SOLAR Phase 1           │
        │     │  Solar P1  │    (Microinverter output)            │
        │     └─────┬─────┘                                      │
        │           │                                             │
        │     ┌─────┴─────┐     ┌──────────┐                     │
        │     │  A2 (ch4)  │◄── │ GRID P1  │ ◄── Utility Meter   │
        │     │  Grid P1   │    │ (bidirec) │     Phase 1         │
        │     └─────┬─────┘     └──────────┘                     │
        │           │                                             │
        │           ├──▶ House loads Phase 1 (NOT directly        │
        │           │    measured — calculated from balance)      │
        │           │                                             │
  ═══ PHASE 2 (B/C) ╪════════════════════════════════════════════╪═══
        │           │                                             │
        │     ┌─────┴─────┐                                      │
        │     │  B2 (ch5)  │◄── CT Clamp: SOLAR Phase 2           │
        │     │  Solar P2  │    (Microinverter output)            │
        │     └─────┬─────┘                                      │
        │           │                                             │
        │     ┌─────┴─────┐     ┌──────────┐                     │
        │     │  C2 (ch6)  │◄── │ GRID P2  │ ◄── Utility Meter   │
        │     │  Grid P2   │    │ (bidirec) │     Phase 2         │
        │     └─────┬─────┘     └──────────┘                     │
        │           │                                             │
        │     ┌─────┴─────┐                                      │
        │     │  B1 (ch2)  │◄── CT Clamp: HOUSE Panel Phase 2     │
        │     │  House P2  │    (Main house loads)                │
        │     └─────┬─────┘                                      │
        │           │                                             │
        │     ┌─────┴─────┐                                      │
        │     │  C1 (ch3)  │◄── CT Clamp: SHOWER Phase 2          │
        │     │  Shower    │    (Electric shower, house load)     │
        │     └────────────┘                                      │
        │                                                         │
        │     ⛏️ Mining loads on SEPARATE circuits                 │
        │     (not measured by any CT — controlled by relays)     │
        └─────────────────────────────────────────────────────────┘
```

### CT Clamp → Refoss Channel Mapping

| Refoss Channel | Index | Phase | Measures | Label |
|:---:|:---:|:---:|:---|:---|
| ch1 / em:1 | idx 0 | **Phase 1** | Solar inverter output | `A1_Solar` |
| ch2 / em:2 | idx 1 | **Phase 2** | House panel loads | `B1_House` |
| ch3 / em:3 | idx 2 | **Phase 2** | Shower (house load) | `C1_Shower` |
| ch4 / em:4 | idx 3 | **Phase 1** | Grid meter | `A2_Grid` |
| ch5 / em:5 | idx 4 | **Phase 2** | Solar inverter output | `B2_Solar` |
| ch6 / em:6 | idx 5 | **Phase 2** | Grid meter | `C2_Grid` |

### Key Observations

- **Phase 1:** Only 2 CTs (Solar + Grid). Home consumption is **NOT directly measured** → must be calculated.
- **Phase 2:** All 4 CTs present (Solar + Grid + House + Shower). Home is **directly measured**.
- **Mining loads** are on separate circuits with no CT clamps — controlled via Tasmota relays + Avalon API.

---

## 📊 Sign Convention

| Quantity | Positive (+) | Negative (−) |
|:---|:---|:---|
| **Solar** | Always positive (generation) | N/A |
| **Grid** | Importing from utility | Exporting to utility |
| **Home** | Always positive (consumption) | N/A |
| **Power Factor (PF)** | Leading (capacitive) | Lagging (inductive) |

---

## 🔢 Per-Channel Calculations

The Refoss EM06P reports **active power** (W) and **power factor** (PF) per channel.
We derive **apparent power** (VA) and **reactive power** (VAR) from these:

### Apparent Power (VA)

```
                    |W|
    VA = ─────────────────    (when |PF| > 0.01 and |W| > 0.1)
                   |PF|

    VA = |W|                  (when PF ≈ 0, treat as purely reactive)
    VA = 0                    (when |W| < 0.1, channel inactive)
```

### Reactive Power (VAR)

```
    VAR = √(VA² − W²)        (Pythagorean relationship)
```

### Power Triangle (per channel)

```
           VA (Apparent)
          ╱│
         ╱ │
        ╱  │ VAR (Reactive)
       ╱   │
      ╱────┘
     W (Active)

    cos(θ) = PF = W / VA
    sin(θ) = VAR / VA
    VA² = W² + VAR²
```

### Example (from live data)

```
    A2_Grid: W = -718.4, PF = -0.67

    VA  = |−718.4| / |−0.67| = 1072.2 VA
    VAR = √(1072.2² − 718.4²) = √(1149,612 − 516,099) = √633,513 = 796.0 VAR
```

---

## ⚡ Per-Phase Energy Balance

> **CRITICAL: Each phase is calculated independently. NO cross-phase vector addition.**
>
> The two phases are on different voltage waveforms (typically 180° apart in split-phase).
> Adding their phasors would be physically meaningless. We sum **scalar magnitudes** only.

### Phase 1 (A channels) — Home is CALCULATED

```
    Solar_P1_W  = |A1_W|                    ← measured (always positive)
    Solar_P1_VA = A1_VA                     ← derived from |A1_W| / |A1_PF|

    Grid_P1_W   = A2_W                      ← measured (signed: − = export)
    Grid_P1_VA  = A2_VA                     ← derived

    Home_P1_W   = Solar_P1_W + Grid_P1_W    ← CALCULATED from energy balance
                                              (if grid negative → exporting → home < solar)
                                              (floor at 0: can't consume negative)
```

**Home Phase 1 — Apparent Power:**

```
    When exporting (Grid_P1 < 0):
        Home_P1_VA = Solar_P1_VA − Grid_P1_VA    (floor at 0)
        (Home uses less VA than solar produces; excess goes to grid)

    When importing (Grid_P1 ≥ 0):
        Home_P1_VA = Solar_P1_VA + Grid_P1_VA
        (Home uses solar + grid VA)
```

**Energy Balance Diagram — Phase 1:**

```
    ┌──────────┐           ┌──────────┐
    │  SOLAR   │──────────▶│   HOME   │
    │  A1_W    │     │     │ (calc'd) │
    │ (always  │     │     │ = S + G  │
    │  positive│     │     └──────────┘
    └──────────┘     │
                     │
                     ▼
               ┌──────────┐
               │   GRID   │
               │  A2_W    │
               │ (signed) │
               │ −=export │
               │ +=import │
               └──────────┘

    Balance: Solar + Grid = Home
    ∴ Home = Solar + Grid  (where Grid is signed)
```

### Phase 2 (B/C channels) — Home is DIRECTLY MEASURED

```
    Solar_P2_W  = |B2_W|                    ← measured
    Solar_P2_VA = B2_VA                     ← derived

    Grid_P2_W   = C2_W                      ← measured (signed)
    Grid_P2_VA  = C2_VA                     ← derived

    Home_P2_W   = |B1_W| + |C1_W|           ← DIRECTLY MEASURED (house + shower)
    Home_P2_VA  = B1_VA + C1_VA             ← DIRECTLY MEASURED
```

**Energy Balance Diagram — Phase 2:**

```
    ┌──────────┐           ┌──────────┐
    │  SOLAR   │──────────▶│   HOME   │
    │  B2_W    │     │     │ |B1|+|C1|│ ◄── MEASURED
    └──────────┘     │     └──────────┘
                     │          │
                     ▼          ▼
               ┌──────────┐  ┌──────────┐
               │   GRID   │  │  SHOWER  │
               │  C2_W    │  │  C1_W    │ ◄── MEASURED
               │ (signed) │  └──────────┘
               └──────────┘
```

---

## 📈 System Totals (Scalar Sum)

> **Both phases are summed as scalar values — NOT as phasors.**

```
    Total_Solar_W   = Solar_P1_W  + Solar_P2_W
    Total_Grid_W    = Grid_P1_W   + Grid_P2_W
    Total_Home_W    = Home_P1_W   + Home_P2_W

    Total_Solar_VA  = Solar_P1_VA + Solar_P2_VA
    Total_Grid_VA   = Grid_P1_VA  + Grid_P2_VA
    Total_Home_VA   = Home_P1_VA  + Home_P2_VA
```

### System Reactive Power

```
    Total_Solar_VAR = √(Total_Solar_VA² − Total_Solar_W²)
    Total_Grid_VAR  = √(Total_Grid_VA²  − Total_Grid_W²)
    Total_Home_VAR  = √(Total_Home_VA²  − Total_Home_W²)
```

---

## 💡 Power Saved (Mining Surplus)

The "power saved" represents the solar surplus available for mining
(what would otherwise be exported to the grid for free or minimal credit):

```
    PowerSaved_W  = Total_Solar_W  − Total_Home_W     (active surplus)
    PowerSaved_VA = Total_Solar_VA − Total_Home_VA     (apparent surplus)
```

- **Positive** → solar exceeds home → surplus available for mining
- **Negative** → home exceeds solar → would need grid power to mine
- Mining decisions use **active W** for profile selection (miners consume resistive loads ≈ PF 1.0)
- **Apparent VA** is tracked/logged to capture the full power picture including reactive loads

---

## 🔄 Data Flow & Timing

```
    Every 30 seconds:
    ┌─────────────┐     ┌──────────────┐     ┌──────────────────┐
    │ Refoss EM06P│────▶│ Parse 6 ch   │────▶│ Per-channel      │
    │ HTTP API    │     │ W, V, A, PF  │     │ VA = |W|/|PF|    │
    │ /rpc/...    │     │              │     │ VAR = √(VA²−W²)  │
    └─────────────┘     └──────────────┘     └────────┬─────────┘
                                                      │
                                                      ▼
                                             ┌──────────────────┐
                                             │ Per-phase totals │
                                             │ Phase1: S,G,H(c) │
                                             │ Phase2: S,G,H(m) │
                                             └────────┬─────────┘
                                                      │
                                                      ▼
                                             ┌──────────────────┐
                                             │ System totals    │
                                             │ Solar, Grid, Home│
                                             │ W, VA, VAR       │
                                             │ PowerSaved W/VA  │
                                             └────────┬─────────┘
                                                      │
                                              Store in aggregation
                                              buffer (up to 24 samples)
                                                      │
    Every 9 minutes:                                  │
    ┌─────────────────────────────────────────────────┘
    │
    ▼
    ┌──────────────────┐     ┌──────────────────┐     ┌────────────────┐
    │ Weighted average │────▶│ Mining decision  │────▶│ Push averages  │
    │ (recent 2× wt)  │     │ Select profile   │     │ to Supabase    │
    │                  │     │ 0-2001W          │     │ (W,VA,VAR,PF)  │
    └──────────────────┘     └──────────────────┘     └────────────────┘
```

---

## 📋 Implementation Logic Flow

```
    [A1 Solar P1]  ───\                                    
    [A2 Grid P1]   ───/──▶ Phase 1 totals ─────────\      
                          Home_P1 = S + G (calc)    │      
                                                    │      
    [B2 Solar P2]  ───\                             ├──▶ System Totals
    [C2 Grid P2]   ───/──▶ Phase 2 totals ─────────/   (scalar sum)
                                                    │      
    [B1 House P2]  ───\                             │      
    [C1 Shower P2] ───/──▶ Home_P2 (measured) ─────/      
                                                           
                                                    ▼      
                                              ┌──────────┐
                                              │ Surplus  │
                                              │ = S − H  │
                                              │ (W & VA) │
                                              └──────────┘
                                                    │
                                                    ▼
                                          Mining Profile Selection
                                          (16 profiles, 0-2001W)
```

---

## 📊 Worked Example (Live Data)

Given these Refoss readings:

| Channel | Active W | PF | Apparent VA | Reactive VAR |
|:---|---:|---:|---:|---:|
| A1_Solar | 1609.1 | 0.90 | 1788.0 | 779.4 |
| B1_House | −843.8 | −0.98 | 861.0 | 171.4 |
| C1_Shower | 0.0 | 0.00 | 0.0 | 0.0 |
| A2_Grid | −718.4 | −0.67 | 1072.2 | 796.0 |
| B2_Solar | 1498.8 | 0.83 | 1805.8 | 1007.7 |
| C2_Grid | −651.8 | −0.51 | 1278.0 | 1099.5 |

### Phase 1 Calculations

```
    Solar_P1_W  = |1609.1| = 1609.1 W
    Solar_P1_VA = 1609.1 / 0.90 = 1788.0 VA

    Grid_P1_W   = −718.4 W  (exporting)
    Grid_P1_VA  = 718.4 / 0.67 = 1072.2 VA

    Home_P1_W   = 1609.1 + (−718.4) = 890.7 W  (calculated!)
    Home_P1_VA  = 1788.0 − 1072.2 = 715.8 VA   (exporting → subtract)
```

### Phase 2 Calculations

```
    Solar_P2_W  = |1498.8| = 1498.8 W
    Solar_P2_VA = 1498.8 / 0.83 = 1805.8 VA

    Grid_P2_W   = −651.8 W  (exporting)
    Grid_P2_VA  = 651.8 / 0.51 = 1278.0 VA

    Home_P2_W   = |−843.8| + |0.0| = 843.8 W  (directly measured!)
    Home_P2_VA  = 861.0 + 0.0 = 861.0 VA       (directly measured!)
```

### System Totals

```
    Total Solar:  1609.1 + 1498.8 = 3107.9 W  |  1788.0 + 1805.8 = 3593.8 VA
    Total Grid:   −718.4 + −651.8 = −1370.2 W  |  1072.2 + 1278.0 = 2350.2 VA
    Total Home:   890.7 + 843.8 = 1734.5 W    |  715.8 + 861.0 = 1576.8 VA

    Power Saved:  3107.9 − 1734.5 = 1373.4 W  |  3593.8 − 1576.8 = 2017.0 VA
```

### Mining Decision

```
    Available surplus = 1373.4 W (active)

    Scanning profiles (highest first):
      S15: AV_HI+BN+OCT  2001W → 2001 > 1373 ✗
      S14: AV_HI+OCT     1900W → 1900 > 1373 ✗
      ...
      S7:  AV_LO+BN+OCT  1061W → 1061 ≤ 1373 ✓  ← SELECTED

    → Switch to profile S7: AV_LO+BN+OCT (1061W)
      Relay R1 ON  (BitAxe 21W + Nerdaxe 60W)
      Relay R2 ON  (Octaxe 180W)
      Avalon Q → Low mode (800W)
```

---

## 🔍 Why Not Vector (Phasor) Addition?

In a split-phase system, Phase 1 and Phase 2 have voltages that are **180° apart**.
Simply adding their complex power phasors would be incorrect because:

1. **Different reference frames:** Each phase has its own voltage reference
2. **Cancellation artifacts:** Reactive components on different phases would partially cancel, giving misleading results
3. **Utility billing:** Grid meters measure per-phase active power, not system-level phasors
4. **Practical accuracy:** For mining surplus decisions, scalar per-phase sums give the correct answer

Therefore, we compute everything **per-phase independently** and then sum the **scalar magnitudes** for system totals.

---

## 📁 Related Files

| File | Description |
|:---|:---|
| [`energy.ino`](../MinerControl_WebUI_FINAL/energy.ino) | Per-phase calculations implementation |
| [`config.h`](../MinerControl_WebUI_FINAL/config.h) | Data structures (`ChannelData`, `PhaseEnergy`) |
| [`refoss.ino`](../MinerControl_WebUI_FINAL/refoss.ino) | Raw data polling from Refoss EM06P |
| [`mining.ino`](../MinerControl_WebUI_FINAL/mining.ino) | Decision engine using surplus W |
| [`supabase.ino`](../MinerControl_WebUI_FINAL/supabase.ino) | Cloud push with W/VA/VAR fields |
| [`web_ui.ino`](../MinerControl_WebUI_FINAL/web_ui.ino) | Dashboard showing per-phase + VA/VAR |
