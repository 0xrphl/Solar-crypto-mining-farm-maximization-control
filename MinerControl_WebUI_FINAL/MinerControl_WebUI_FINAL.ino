/*
 * MinerControl_WebUI_FINAL.ino — Main file (setup + loop + globals)
 *
 * ESP32 Solar Mining Control + Refoss EMO6P + Supabase
 *
 * Polls Refoss EMO6P 6-ch energy monitor via local HTTP API every 30s,
 * calculates per-phase active/apparent/reactive power (scalar, no vector),
 * aggregates samples, pushes 9-min averages to Supabase REST API,
 * controls relays and miners based on solar surplus.
 *
 * 2-Phase (Bi-Phase) Circuit:
 *   Phase 1: A1=Solar, A2=Grid, Home=calculated
 *   Phase 2: B1=House, B2=Solar, C1=Shower, C2=Grid (all measured)
 *
 * File structure:
 *   config.h          — structs, constants, profiles, miner defs
 *   credentials.h     — WiFi/Tasmota/Supabase/Refoss passwords (gitignored)
 *   display.ino        — TFT display functions
 *   refoss.ino         — Refoss EMO6P discovery + polling + JSON parsing
 *   energy.ino         — Per-phase energy calculations (VA/VAR/W)
 *   mining.ino         — Mining decision engine + state verification
 *   supabase.ino       — Supabase REST API push
 *   relay_control.ino  — Tasmota relay + BitAxe + Avalon control
 *   web_ui.ino         — Web server handlers + HTML/CSS/JS
 *
 * Access: http://[ESP32_IP]
 */

#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>
#include <time.h>
#include <math.h>
#include <MD5Builder.h>
#include <Preferences.h>
#include "config.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)
#error "Use Arduino ESP32 version < 3.0"
#endif

// ==================== NETWORK CONFIG ====================

const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

const char* tasmotaIP       = "192.168.1.78";
const char* tasmotaUser     = TASMOTA_USER;
const char* tasmotaPassword = TASMOTA_PASSWORD;

const char* SUPABASE_URL    = SUPABASE_URL_STR;
const char* SUPABASE_APIKEY = SUPABASE_APIKEY_STR;

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -18000;
const int   daylightOffset_sec = 0;

// Refoss EMO6P
String refossIP   = "192.168.1.82";
String refossUUID = "";
String refossKey  = "";
bool   refossFound = false;

// ==================== DISPLAY ====================

#define GFX_EXTRA_PRE_INIT() { pinMode(15, OUTPUT); digitalWrite(15, HIGH); }

Arduino_DataBus *bus = new Arduino_ESP32PAR8Q(
    7, 6, 8, 9, 39, 40, 41, 42, 45, 46, 47, 48);
Arduino_GFX *gfx = new Arduino_ST7789(
    bus, 5, 3, true, 170, 320, 35, 0, 35, 0);

WebServer server(80);
WiFiUDP   udp;

String lastAction = "";
String espIP      = "";

// ==================== ENERGY DATA ====================

// 6 channels: idx 0-5 = Refoss ch1-ch6 = A1,B1,C1,A2,B2,C2
ChannelData liveCh[6];

// System totals — active power (W)
float liveSolar = 0, liveGrid = 0, liveHome = 0;

// System totals — apparent power (VA)
float liveSolarVA = 0, liveGridVA = 0, liveHomeVA = 0;

// System totals — reactive power (VAR)
float liveSolarVAR = 0, liveGridVAR = 0, liveHomeVAR = 0;

// Power saved (surplus available for mining)
float livePowerSaved = 0, livePowerSavedVA = 0;

// Per-phase energy totals
PhaseEnergy phase1, phase2;

// ==================== AGGREGATION BUFFER ====================
// Stores 30s samples for 9-min averaging before Supabase push

float aggPower[6][MAX_SAMPLES];
float aggVoltage[3][MAX_SAMPLES];
float aggCurrent[6][MAX_SAMPLES];
float aggPf[6][MAX_SAMPLES];
float aggEnergy[6][MAX_SAMPLES];
float aggVA[6][MAX_SAMPLES];
float aggVAR[6][MAX_SAMPLES];
float aggSolar[MAX_SAMPLES], aggGrid[MAX_SAMPLES], aggHome[MAX_SAMPLES];
float aggSolarVA[MAX_SAMPLES], aggGridVA[MAX_SAMPLES], aggHomeVA[MAX_SAMPLES];
float aggSaved[MAX_SAMPLES], aggSavedVA[MAX_SAMPLES];
int   aggCount = 0;

// ==================== TIMING ====================

unsigned long lastRefossPoll  = 0;
unsigned long lastDecision    = 0;
unsigned long lastRefossDisc  = 0;
unsigned long lastAvalonCmdTime = 0;
bool refossDataValid = false;

// ==================== MINING STATE ====================

int miningState = 0;
bool autoMiningEnabled = true;
Preferences prefs;

// Actual device state (populated by verifyMinerStates)
String actualAvalonMode = "unknown";
float  actualAvalonMHS  = 0;
int    actualAvalonMPO  = 0;
float  actualBitaxeHR[3] = {0};
float  actualBitaxePwr[3] = {0};
bool   actualBitaxeOn[3] = {false};
String lastVerifyResult = "";

// Deferred Avalon workmode — set after wakeup, applied once Avalon is booted
String pendingAvalonMode = "";          // "" = nothing pending, "low"/"mid"/"high"
unsigned long pendingAvalonSetTime = 0; // millis() when pending was set
int pendingAvalonRetries = 0;           // retry counter (max 5)

// ==================== SETUP ====================

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println(F("\n========================================"));
    Serial.println(F("  ESP32 Solar Mining Control + Supabase"));
    Serial.println(F("  2-Phase Energy Monitor (W/VA/VAR)"));
    Serial.println(F("========================================\n"));

    GFX_EXTRA_PRE_INIT();
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);

    gfx->begin();
    gfx->setRotation(3);
    gfx->fillScreen(BLACK);
    displayMessage("WiFi", "Connecting...", YELLOW);

    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500); Serial.print("."); attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        espIP = WiFi.localIP().toString();
        Serial.print("\nConnected: "); Serial.println(espIP);
        displayMessage("WiFi OK", espIP, GREEN);
        delay(1000);

        // NTP sync
        Serial.print("Syncing time...");
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        int ntpAtt = 0;
        time_t now = time(nullptr);
        while (now < 1000000000 && ntpAtt < 20) {
            delay(500); now = time(nullptr); ntpAtt++;
        }
        if (now > 1000000000) {
            struct tm ti; getLocalTime(&ti);
            Serial.println(" OK");
            Serial.print("Time: "); Serial.println(&ti, "%Y-%m-%d %H:%M:%S");
        } else {
            Serial.println(" FAILED");
        }
        delay(500);
    } else {
        Serial.println("\nWiFi FAILED");
        displayMessage("WiFi FAIL", "Check network", RED);
        delay(3000);
        return;
    }

    // Init aggregation buffers
    memset(aggPower, 0, sizeof(aggPower));
    memset(aggVoltage, 0, sizeof(aggVoltage));
    memset(aggCurrent, 0, sizeof(aggCurrent));
    memset(aggPf, 0, sizeof(aggPf));
    memset(aggEnergy, 0, sizeof(aggEnergy));
    memset(aggVA, 0, sizeof(aggVA));
    memset(aggVAR, 0, sizeof(aggVAR));
    memset(aggSolar, 0, sizeof(aggSolar));
    memset(aggGrid, 0, sizeof(aggGrid));
    memset(aggHome, 0, sizeof(aggHome));
    memset(aggSolarVA, 0, sizeof(aggSolarVA));
    memset(aggGridVA, 0, sizeof(aggGridVA));
    memset(aggHomeVA, 0, sizeof(aggHomeVA));
    memset(aggSaved, 0, sizeof(aggSaved));
    memset(aggSavedVA, 0, sizeof(aggSavedVA));

    // Init per-phase structs
    memset(&phase1, 0, sizeof(phase1));
    memset(&phase2, 0, sizeof(phase2));

    // Restore miningState from NVS
    prefs.begin("mining", false);
    miningState = prefs.getInt("state", 0);
    prefs.end();
    Serial.print("Restored miningState from NVS: S");
    Serial.print(miningState);
    Serial.print(" ("); Serial.print(profiles[miningState].name);
    Serial.println(")");

    // Discover Refoss EMO6P
    discoverRefoss();

    // Boot-time: verify actual device states
    Serial.println("\n=== BOOT-TIME STATE RECONCILIATION ===");
    displayMessage("Verifying", "Checking miners...", YELLOW);
    delay(500);
    verifyMinerStates();
    Serial.println("=== BOOT RECONCILIATION COMPLETE ===\n");

    // Web server routes
    server.on("/", handleRoot);
    server.on("/restart", handleRestart);
    server.on("/status", handleStatus);
    server.on("/avalon", handleAvalon);
    server.on("/relay", handleRelay);
    server.on("/relaystatus", handleRelayStatus);
    server.on("/energy", handleEnergy);
    server.on("/supabase", handleSupabaseSync);
    server.onNotFound(handleNotFound);

    server.begin();
    Serial.println("Web server started at http://" + espIP);
    showWebUI();
}

// ==================== LOOP ====================

void loop() {
    server.handleClient();

    unsigned long now = millis();

    // Discover Refoss if not found yet
    if (!refossFound && (now - lastRefossDisc > REFOSS_DISC_MS || lastRefossDisc == 0)) {
        lastRefossDisc = now;
        discoverRefoss();
    }

    // Poll Refoss EMO6P energy data every 30s
    if (refossFound && (now - lastRefossPoll > REFOSS_POLL_MS)) {
        lastRefossPoll = now;
        pollRefossEnergy();

        // Check for deferred Avalon workmode (set after wakeup from correction)
        checkPendingAvalonMode();
    }

    // Every 9 min: decision cycle for ALL miners + push to Supabase
    if (refossDataValid && (now - lastDecision > DECISION_CYCLE_MS || lastDecision == 0)) {
        lastDecision = now;

        runMiningDecision();
        delay(2000);
        verifyMinerStates();
        pushToSupabase();

        Serial.println(">> 9-min cycle: decision + verify + push");
    }

    delay(10);
}
