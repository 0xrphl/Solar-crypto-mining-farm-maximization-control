/*
 * config.h — Configuration, structs, constants, mining profiles
 *
 * ESP32 Solar Mining Control + Refoss EMO6P + Supabase
 *
 * 2-Phase (Bi-Phase) Circuit Layout:
 *   Phase 1 (A channels):
 *     A1 (ch1) = Solar CT        — measured
 *     A2 (ch4) = Grid CT         — measured
 *     Home Phase 1               — CALCULATED: Home_P1 = Solar_P1 + Grid_P1
 *
 *   Phase 2 (B/C channels — "Shower phase", ALL clamps known):
 *     B1 (ch2) = House CT        — measured
 *     B2 (ch5) = Solar CT        — measured
 *     C1 (ch3) = Shower CT       — measured (house load)
 *     C2 (ch6) = Grid CT         — measured
 *     Home Phase 2               — DIRECTLY measured: |B1| + |C1|
 *
 * Sign convention:
 *   Solar: always positive (generation)
 *   Grid:  negative = exporting, positive = importing
 *   Home:  always positive (consumption)
 *
 * NOTE: Refoss EM06P "power" field = ACTIVE power (W), not apparent (VA).
 *       PF is reported separately. Apparent = |W|/|PF|, Reactive = sqrt(VA²-W²)
 *       Per-phase scalar calculations only — NO cross-phase vector addition.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "credentials.h"

// ==================== HARDWARE PINS ====================
#define BUTTON_PIN 14
#define GFX_BL     38

// ==================== INTERVALS (ms) ====================
#define REFOSS_POLL_MS          30000     // Poll energy every 30s
#define DECISION_CYCLE_MS       540000    // 9 min — single decision cycle for ALL miners
#define REFOSS_DISC_MS          60000     // Retry Refoss discovery every 60s
#define AVALON_CMD_COOLDOWN_MS  30000     // 30s cooldown between Avalon commands

// ==================== AGGREGATION ====================
#define MAX_SAMPLES 24   // 9 min ÷ 30s = up to 18 samples per cycle (with headroom)

// ==================== DATA STRUCTURES ====================

struct ChannelData {
    float power_w;       // Active power (W) — raw from Refoss
    float voltage_v;
    float current_a;
    float pf;            // Power factor (signed: +/- indicates lead/lag)
    float energy_wh;     // Accumulated energy
    float apparent_va;   // Apparent power = |W| / |PF|
    float reactive_var;  // Reactive power = sqrt(VA² - W²)
};

// Per-phase energy totals (scalar, no vector addition across phases)
struct PhaseEnergy {
    float solar_w;       // Active solar power this phase
    float grid_w;        // Active grid power (signed: - export, + import)
    float home_w;        // Active home consumption this phase
    float solar_va;      // Apparent solar power
    float grid_va;       // Apparent grid power
    float home_va;       // Apparent home consumption
};

struct Miner {
    const char* name;
    const char* ip;
    int         port;
    const char* type;
    int         relayGroup; // 1=R1 BitAxe+Nerd, 2=R2 Octaxe, 0=API
};

struct MiningProfile {
    const char* name;
    int   totalW;
    bool  relay1;           // R1: BitAxe+Nerdaxe
    bool  relay2;           // R2: Octaxe
    const char* avalonMode; // "off","low","mid","high"
};

// ==================== MINER DEFINITIONS ====================

static Miner miners[] = {
    {"BitAxe Rafa", "192.168.1.21", 80,   "bitaxe", 1},
    {"Nerdaxe",     "192.168.1.28", 80,   "bitaxe", 1},
    {"Octaxe",      "192.168.1.37", 80,   "bitaxe", 2},
    {"Avalon Q",    "192.168.1.51", 4028, "avalon",  0}
};
static const int numMiners = 4;

// ==================== MINING PROFILES ====================
// All 16 combinations of: Avalon(off/800/1600/1720) × R1(BitAxe+Nerd 81W) × R2(Octaxe 180W)
// Sorted by total watts ascending.
// R1 = BitAxe 21W + Nerdaxe 60W = 81W
// R2 = Octaxe = 180W
// Avalon Q: off=0, low=800, mid=1600, high=1720

static const MiningProfile profiles[] = {
    {"OFF",              0,    false, false, "off"},   //  0
    {"BN",               81,   true,  false, "off"},   //  1  BitAxe+Nerd only
    {"OCT",              180,  false, true,  "off"},   //  2  Octaxe only
    {"BN+OCT",           261,  true,  true,  "off"},   //  3  both small miners
    {"AV_LO",            800,  false, false, "low"},   //  4  Avalon low only
    {"AV_LO+BN",         881,  true,  false, "low"},   //  5
    {"AV_LO+OCT",        980,  false, true,  "low"},   //  6
    {"AV_LO+BN+OCT",     1061, true,  true,  "low"},   //  7
    {"AV_MD",            1600, false, false, "mid"},   //  8  Avalon mid only
    {"AV_MD+BN",         1681, true,  false, "mid"},   //  9
    {"AV_HI",            1720, false, false, "high"},  // 10  Avalon high only
    {"AV_MD+OCT",        1780, false, true,  "mid"},   // 11
    {"AV_HI+BN",         1801, true,  false, "high"},  // 12
    {"AV_MD+BN+OCT",     1861, true,  true,  "mid"},   // 13
    {"AV_HI+OCT",        1900, false, true,  "high"},  // 14
    {"AV_HI+BN+OCT",     2001, true,  true,  "high"}   // 15  MAX
};
static const int NUM_PROFILES = 16;

// ==================== CHANNEL NAME LABELS ====================
static const char* chNames[] = {"A1_Solar","B1_House","C1_Shower","A2_Grid","B2_Solar","C2_Grid"};

#endif // CONFIG_H
