/*
 * ESP32 Solar Mining Control + Refoss EMO6P + Supabase
 *
 * Polls Refoss EMO6P 6-ch energy monitor via local HTTP API,
 * aggregates 5-min averages, pushes to Supabase REST API,
 * controls relays and miners based on solar/grid data.
 *
 * Relay mapping:  R1 = BitAxe+Nerdaxe (81W),  R2 = Octaxe (180W)
 * Avalon Q = API-only control (no relay)
 *
 * Channel map (Refoss EMO6P, bi-phase):
 *   ch1=A1 Solar P1       ch4=A2 Grid P1
 *   ch2=B1 House P2       ch5=B2 Solar P2
 *   ch3=C1 Shower (House) ch6=C2 Grid P2
 *   Solar = A1+|B2|   Grid = A2+C2 (direct)   House = |B1|+|C1| (direct)
 *
 * NOTE: Refoss EM06P "power" field = ACTIVE power (W), not apparent (VA).
 *       PF is reported separately. No additional PF multiplication needed.
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
#include <MD5Builder.h>
#include <Preferences.h>    // NVS flash — persist miningState across reboots
#include "credentials.h"   // ⚠️ Real creds in credentials.h (gitignored)
                            // Copy credentials.h.example → credentials.h

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,0,0)
#error "Use Arduino ESP32 version < 3.0"
#endif

// ==================== CONFIGURATION ====================

const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// Tasmota Relay (ESP32 2x relay board)
const char* tasmotaIP       = "192.168.1.78";
const char* tasmotaUser     = TASMOTA_USER;
const char* tasmotaPassword = TASMOTA_PASSWORD;

// Supabase REST API
const char* SUPABASE_URL    = SUPABASE_URL_STR;
const char* SUPABASE_APIKEY = SUPABASE_APIKEY_STR;

// NTP
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -18000;
const int   daylightOffset_sec = 0;

// Refoss EMO6P — known IP, UUID fetched via HTTP
String refossIP   = "192.168.1.82";  // known IP from router
String refossUUID = "";
String refossKey  = "";              // signing key (try empty, then password)
bool   refossFound = false;

// Intervals (ms)
#define REFOSS_POLL_MS    30000
#define SUPABASE_PUSH_MS  300000
#define REFOSS_DISC_MS    60000

// Miners grouped by relay
struct Miner {
    const char* name;
    const char* ip;
    int         port;
    const char* type;
    int         relayGroup; // 1=R1 BitAxe+Nerd, 2=R2 Octaxe, 0=API
};

Miner miners[] = {
    {"BitAxe Rafa", "192.168.1.21", 80,   "bitaxe", 1},
    {"Nerdaxe",     "192.168.1.28", 80,   "bitaxe", 1},
    {"Octaxe",      "192.168.1.37", 80,   "bitaxe", 2},
    {"Avalon Q",    "192.168.1.51", 4028, "avalon",  0}
};
const int numMiners = 4;

#define BUTTON_PIN 14
#define GFX_BL     38

WebServer server(80);
WiFiUDP   udp;

// ==================== DISPLAY ====================

#define GFX_EXTRA_PRE_INIT() { pinMode(15, OUTPUT); digitalWrite(15, HIGH); }

Arduino_DataBus *bus = new Arduino_ESP32PAR8Q(
    7, 6, 8, 9, 39, 40, 41, 42, 45, 46, 47, 48);
Arduino_GFX *gfx = new Arduino_ST7789(
    bus, 5, 3, true, 170, 320, 35, 0, 35, 0);

String lastAction = "";
String espIP      = "";

// ==================== ENERGY DATA ====================

struct ChannelData {
    float power_w;
    float voltage_v;
    float current_a;
    float pf;
    float energy_wh;
};

// 6 channels: idx 0-5 = Refoss ch1-ch6 = A1,B1,C1,A2,B2,C2
ChannelData liveCh[6];
float liveSolar = 0, liveGrid = 0, liveHome = 0;

// Aggregation buffer
#define MAX_SAMPLES 12
float aggPower[6][MAX_SAMPLES];
float aggVoltage[3][MAX_SAMPLES];
float aggCurrent[6][MAX_SAMPLES];
float aggPf[6][MAX_SAMPLES];
float aggEnergy[6][MAX_SAMPLES];
float aggSolar[MAX_SAMPLES], aggGrid[MAX_SAMPLES], aggHome[MAX_SAMPLES];
int   aggCount = 0;

unsigned long lastRefossPoll  = 0;
unsigned long lastSupaPush    = 0;
unsigned long lastRefossDisc  = 0;
bool refossDataValid = false;

// ==================== MINING DECISION ENGINE ====================
// All 16 combinations of: Avalon(off/800/1600/1720) × R1(BitAxe+Nerd 81W) × R2(Octaxe 180W)
// Sorted by total watts ascending.
// Every 5 min: jump to the highest profile that fits UNDER the surplus.
// R1 = BitAxe 21W + Nerdaxe 60W = 81W
// R2 = Octaxe = 180W
// Avalon Q: off=0, low=800, mid=1600, high=1720

int miningState = 0;
bool autoMiningEnabled = true;
Preferences prefs;  // NVS flash storage for persistent state

// Actual device state (populated by verifyMinerStates)
String actualAvalonMode = "unknown";  // "off","sleep","low","mid","high","unknown"
float  actualAvalonMHS  = 0;          // MH/s from summary
int    actualAvalonMPO  = 0;          // watts from estats
float  actualBitaxeHR[3] = {0};       // hashrate for miners[0..2]
float  actualBitaxePwr[3] = {0};      // power for miners[0..2]
bool   actualBitaxeOn[3] = {false};   // reachable + hashing?
String lastVerifyResult = "";         // last verify summary for UI

struct MiningProfile {
    const char* name;
    int   totalW;
    bool  relay1;           // R1: BitAxe+Nerdaxe
    bool  relay2;           // R2: Octaxe
    const char* avalonMode; // "off","low","mid","high"
};

// All 16 combos sorted by totalW ascending
const MiningProfile profiles[] = {
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
const int NUM_PROFILES = 16;

// ==================== SETUP ====================

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println(F("\n========================================"));
    Serial.println(F("  ESP32 Solar Mining Control + Supabase"));
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

    // Init aggregation buffer
    memset(aggPower, 0, sizeof(aggPower));
    memset(aggVoltage, 0, sizeof(aggVoltage));
    memset(aggCurrent, 0, sizeof(aggCurrent));
    memset(aggPf, 0, sizeof(aggPf));
    memset(aggEnergy, 0, sizeof(aggEnergy));
    memset(aggSolar, 0, sizeof(aggSolar));
    memset(aggGrid, 0, sizeof(aggGrid));
    memset(aggHome, 0, sizeof(aggHome));

    // Restore miningState from NVS (survives reboots)
    prefs.begin("mining", false);
    miningState = prefs.getInt("state", 0);
    prefs.end();
    Serial.print("Restored miningState from NVS: S");
    Serial.print(miningState);
    Serial.print(" ("); Serial.print(profiles[miningState].name);
    Serial.println(")");

    // Try to discover Refoss EMO6P
    discoverRefoss();

    // Boot-time: verify actual device states match persisted state
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

    // Poll Refoss EMO6P energy data
    if (refossFound && (now - lastRefossPoll > REFOSS_POLL_MS)) {
        lastRefossPoll = now;
        pollRefossEnergy();
    }

    // Every 5 min: run mining decision THEN push to Supabase
    if (refossDataValid && (now - lastSupaPush > SUPABASE_PUSH_MS)) {
        lastSupaPush = now;
        verifyMinerStates();  // verify actual states match expected (power outage recovery)
        runMiningDecision();  // decide FIRST on averaged data
        pushToSupabase();     // then push data to cloud
    }

    delay(10);
}

// ==================== DISPLAY FUNCTIONS ====================

void displayMessage(String title, String message, uint16_t color) {
    gfx->fillScreen(BLACK);
    gfx->setRotation(3);
    gfx->setTextSize(2);
    gfx->setTextColor(color);
    gfx->setCursor(10, 10);
    gfx->println(title);
    gfx->setTextSize(1);
    gfx->setCursor(10, 40);
    gfx->println(message);
}

void showWebUI() {
    gfx->fillScreen(BLACK);
    gfx->setRotation(3);
    gfx->setTextSize(2);
    gfx->setTextColor(CYAN);
    gfx->setCursor(10, 5);
    gfx->println("Solar Miner");

    gfx->setTextSize(1);
    gfx->setTextColor(GREEN);
    gfx->setCursor(10, 30);
    gfx->print("http://"); gfx->println(espIP);

    // Energy summary line
    gfx->setTextColor(YELLOW);
    gfx->setCursor(10, 48);
    if (refossDataValid) {
        gfx->print("Solar:"); gfx->print(liveSolar, 0); gfx->print("W ");
        gfx->print("Grid:"); gfx->print(liveGrid, 0); gfx->print("W ");
        gfx->print("Home:"); gfx->print(liveHome, 0); gfx->print("W");
    } else if (refossFound) {
        gfx->print("EMO6P @ "); gfx->print(refossIP); gfx->print(" ...");
    } else {
        gfx->print("Searching Refoss EMO6P...");
    }

    // Relay status line
    gfx->setTextColor(WHITE);
    gfx->setCursor(10, 66);
    gfx->print("R1:BN(81W) R2:Oct(180W) S"); gfx->print(miningState);

    // Last action
    if (lastAction.length() > 0) {
        gfx->setTextColor(MAGENTA);
        gfx->setCursor(10, 84);
        gfx->println(lastAction.substring(0, 40));
    }

    // Supabase status
    gfx->setTextColor(0x07E0); // bright green
    gfx->setCursor(10, 100);
    if (lastSupaPush > 0) {
        unsigned long ago = (millis() - lastSupaPush) / 1000;
        gfx->print("Supabase: "); gfx->print(ago); gfx->print("s ago");
        gfx->print(" ("); gfx->print(aggCount); gfx->print(" samples)");
    } else {
        gfx->print("Supabase: waiting first push");
    }
}

// ==================== REFOSS EMO6P (HTTP API) ====================

// The Refoss EMO6P has a Shelly-compatible web API.
// We try multiple endpoints to find which one works.

void discoverRefoss() {
    Serial.print("Probing Refoss EMO6P @ ");
    Serial.print(refossIP);
    Serial.println("...");

    // Try direct HTTP to get device info and UUID
    // Use Appliance.System.All namespace to get device details
    String rnd = "";
    for (int i = 0; i < 16; i++) rnd += (char)('A' + random(0, 26));

    MD5Builder md5;
    md5.begin(); md5.add(rnd); md5.calculate();
    String messageId = md5.toString();
    unsigned long ts = getUnixTimestamp();

    MD5Builder md5s;
    md5s.begin(); md5s.add(messageId + String(ts)); md5s.calculate();
    String sign = md5s.toString();

    String body = "{\"header\":{";
    body += "\"from\":\"/app/" + rnd + "/subscribe\",";
    body += "\"messageId\":\"" + messageId + "\",";
    body += "\"method\":\"GET\",";
    body += "\"namespace\":\"Appliance.System.All\",";
    body += "\"payloadVersion\":1,";
    body += "\"sign\":\"" + sign + "\",";
    body += "\"timestamp\":" + String(ts) + ",";
    body += "\"triggerSrc\":\"ESP32\",";
    body += "\"uuid\":\"\"";
    body += "},\"payload\":{}}";

    HTTPClient http;
    String url = "http://" + refossIP + "/public";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);

    int httpCode = http.POST(body);

    if (httpCode == 200) {
        String resp = http.getString();
        http.end();

        Serial.print("Refoss response (");
        Serial.print(resp.length());
        Serial.println(" bytes)");

        // Extract UUID from response
        int ui = resp.indexOf("\"uuid\":\"");
        if (ui >= 0) {
            int ue = resp.indexOf("\"", ui + 8);
            refossUUID = resp.substring(ui + 8, ue);
        }

        // If no UUID found, generate a dummy one — some devices
        // accept empty UUID in requests
        if (refossUUID.length() == 0) {
            refossUUID = "refoss_emo6p";
        }

        refossFound = true;
        Serial.print("Refoss EMO6P OK @ ");
        Serial.print(refossIP);
        Serial.print(" UUID: ");
        Serial.println(refossUUID);
        displayMessage("Refoss OK", refossIP, GREEN);
        delay(500);
    } else {
        http.end();
        Serial.print("Refoss probe failed: HTTP ");
        Serial.println(httpCode);

        // Also try UDP broadcast as fallback
        Serial.println("Trying UDP broadcast fallback...");
        udp.begin(9989);
        String msg = "{\"id\":\"esp32miner\",\"devName\":\"*\"}";
        udp.beginPacket("255.255.255.255", 9988);
        udp.print(msg);
        udp.endPacket();

        unsigned long start = millis();
        while (millis() - start < 3000) {
            int packetSize = udp.parsePacket();
            if (packetSize > 0) {
                char buf[1024];
                int len = udp.read(buf, sizeof(buf) - 1);
                buf[len] = '\0';
                String resp = String(buf);
                Serial.print("UDP response: ");
                Serial.println(resp);

                if (resp.indexOf("\"uuid\"") >= 0) {
                    int ui2 = resp.indexOf("\"uuid\":\"");
                    if (ui2 >= 0) {
                        int ue2 = resp.indexOf("\"", ui2 + 8);
                        refossUUID = resp.substring(ui2 + 8, ue2);
                    }
                    refossIP = udp.remoteIP().toString();
                    refossFound = true;
                    Serial.print("Found via UDP @ ");
                    Serial.println(refossIP);
                    break;
                }
            }
            delay(50);
        }
        udp.stop();

        if (!refossFound) {
            Serial.println("Refoss EMO6P not found (will retry)");
        }
    }
}

// ==================== REFOSS EMO6P POLLING ====================

// Try multiple HTTP endpoints to find the device's API
int refossApiType = -1;  // -1=auto-detect, 0=/rpc/EM.GetStatus, 1=/status, 2=/meter/0

void pollRefossEnergy() {
    if (!refossFound || refossIP.length() == 0) return;

    HTTPClient http;
    http.setTimeout(5000);
    String resp = "";
    int httpCode = 0;

    // Refoss EM06P uses dot-separated method names:
    //   Refoss.Status.Get  (NOT Refoss.GetStatus)
    //   Values in real units: W, V, A (NOT mW/mV/mA)
    const char* endpoints[] = {
        "/rpc/Refoss.Status.Get"
    };
    const int numEndpoints = 1;
    const char* refossPass = REFOSS_PASSWORD;

    if (refossApiType >= 0) {
        String url = "http://" + refossIP + endpoints[refossApiType];
        http.begin(url);
        http.setAuthorization("admin", refossPass);
        httpCode = http.GET();
        if (httpCode == 200) resp = http.getString();
        http.end();
    } else {
        // Auto-detect: try each endpoint with and without auth
        for (int i = 0; i < numEndpoints; i++) {
            String url = "http://" + refossIP + endpoints[i];
            Serial.print("Trying: "); Serial.println(url);

            // Try WITHOUT auth first
            http.begin(url);
            httpCode = http.GET();
            if (httpCode == 200) {
                resp = http.getString();
                http.end();
                Serial.print("  200 ("); Serial.print(resp.length()); Serial.print("b): ");
                Serial.println(resp.substring(0, min(200, (int)resp.length())));
                // Accept if has JSON with actual data (not error/invalid)
                if (resp.length() > 20 && resp.indexOf("{") >= 0
                    && resp.indexOf("invalid") < 0 && resp.indexOf("error") < 0) {
                    refossApiType = i;
                    Serial.print(">>> USING: "); Serial.println(endpoints[i]);
                    break;
                }
                resp = "";
                // fall through to try WITH auth
            } else {
                http.end();
                Serial.print("  no-auth HTTP "); Serial.println(httpCode);
            }

            // Try WITH digest/basic auth
            http.begin(url);
            http.setAuthorization("admin", refossPass);
            httpCode = http.GET();
            if (httpCode == 200) {
                resp = http.getString();
                http.end();
                Serial.print("  AUTH 200 ("); Serial.print(resp.length()); Serial.print("b): ");
                Serial.println(resp.substring(0, min(200, (int)resp.length())));
                if (resp.length() > 20 && resp.indexOf("{") >= 0
                    && resp.indexOf("invalid") < 0 && resp.indexOf("error") < 0) {
                    refossApiType = i;
                    Serial.print(">>> USING (auth): "); Serial.println(endpoints[i]);
                    break;
                }
                resp = "";
            } else {
                http.end();
                Serial.print("  auth HTTP "); Serial.println(httpCode);
            }
        }
    }

    if (httpCode != 200 || resp.length() == 0) {
        Serial.print("Refoss poll failed: HTTP "); Serial.println(httpCode);
        return;
    }

    // Debug: print response
    Serial.print("Refoss ("); Serial.print(resp.length()); Serial.print("b): ");
    Serial.println(resp.substring(0, min(500, (int)resp.length())));

    // Parse the response — handles multiple possible JSON formats
    // Sets liveCh[], liveSolar, liveGrid, liveHome, refossDataValid
    parseRefossResponse(resp);

    // Store in aggregation buffer
    if (aggCount < MAX_SAMPLES) {
        int s = aggCount;
        for (int i = 0; i < 6; i++) {
            aggPower[i][s] = liveCh[i].power_w;
            aggCurrent[i][s] = liveCh[i].current_a;
            aggPf[i][s] = liveCh[i].pf;
            aggEnergy[i][s] = liveCh[i].energy_wh;
        }
        aggVoltage[0][s] = liveCh[0].voltage_v; // Phase 1
        aggVoltage[1][s] = liveCh[1].voltage_v; // Phase 2
        aggVoltage[2][s] = liveCh[2].voltage_v; // Phase 3
        aggSolar[s] = liveSolar;
        aggGrid[s]  = liveGrid;
        aggHome[s]  = liveHome;
        aggCount++;
    }

    Serial.print("Energy: Solar="); Serial.print(liveSolar, 0);
    Serial.print("W Grid="); Serial.print(liveGrid, 0);
    Serial.print("W Home="); Serial.print(liveHome, 0);
    Serial.print("W ("); Serial.print(aggCount); Serial.println(" samples)");

    showWebUI();
}

// Parse response from Refoss — handles multiple API formats
void parseRefossResponse(String resp) {
    // Try to find any power/energy data in the JSON response
    // Shelly-style: {"a_act_power":X,"a_aprt_power":X,"a_current":X,"a_voltage":X,...}
    // or: {"em:0":{"a_act_power":X,...}}
    // or channel-based: [{"channel":1,"power":X,...}]

    bool found = false;

    // Format 1: Shelly EM — keys like a_act_power, b_act_power, c_act_power
    if (resp.indexOf("act_power") >= 0) {
        // Phase A = channels 0,3 (A1 Solar, A2 Home)
        liveCh[0].power_w = extractJsonFloat(resp, "\"a_act_power\":");
        liveCh[0].voltage_v = extractJsonFloat(resp, "\"a_voltage\":");
        liveCh[0].current_a = extractJsonFloat(resp, "\"a_current\":");
        liveCh[0].pf = extractJsonFloat(resp, "\"a_pf\":");

        // Phase B = channels 1,4 (B1 Grid, B2 Solar)
        liveCh[1].power_w = extractJsonFloat(resp, "\"b_act_power\":");
        liveCh[1].voltage_v = extractJsonFloat(resp, "\"b_voltage\":");
        liveCh[1].current_a = extractJsonFloat(resp, "\"b_current\":");
        liveCh[1].pf = extractJsonFloat(resp, "\"b_pf\":");

        // Phase C = channels 2,5 (C1 Shower, C2 Home)
        liveCh[2].power_w = extractJsonFloat(resp, "\"c_act_power\":");
        liveCh[2].voltage_v = extractJsonFloat(resp, "\"c_voltage\":");
        liveCh[2].current_a = extractJsonFloat(resp, "\"c_current\":");
        liveCh[2].pf = extractJsonFloat(resp, "\"c_pf\":");

        // Total power
        float totalP = extractJsonFloat(resp, "\"total_act_power\":");

        // For 6-channel: look for secondary CTs (a2, b2, c2)
        // Some firmwares use ct1/ct2 per phase
        liveCh[3].power_w = extractJsonFloat(resp, "\"a2_act_power\":");
        liveCh[4].power_w = extractJsonFloat(resp, "\"b2_act_power\":");
        liveCh[5].power_w = extractJsonFloat(resp, "\"c2_act_power\":");
        liveCh[3].current_a = extractJsonFloat(resp, "\"a2_current\":");
        liveCh[4].current_a = extractJsonFloat(resp, "\"b2_current\":");
        liveCh[5].current_a = extractJsonFloat(resp, "\"c2_current\":");

        found = true;
        Serial.println("Parsed Shelly EM format");
    }
    // Format 2: Refoss RPC — "em:1":{"power":4.742,"voltage":123.921,"current":0.108,...}
    // Values from Refoss.Status.Get are in REAL units: W, V, A
    else if (resp.indexOf("\"em:") >= 0 || resp.indexOf("\"power\"") >= 0) {
        for (int ch = 1; ch <= 6; ch++) {
            String emKey = "\"em:" + String(ch) + "\"";
            int ei = resp.indexOf(emKey);
            if (ei >= 0) {
                int os = resp.indexOf("{", ei);
                int oe = resp.indexOf("}", os);
                if (os >= 0 && oe >= 0) {
                    String obj = resp.substring(os, oe + 1);
                    int idx = ch - 1;
                    liveCh[idx].power_w = extractJsonFloat(obj, "\"power\":");
                    liveCh[idx].voltage_v = extractJsonFloat(obj, "\"voltage\":");
                    liveCh[idx].current_a = extractJsonFloat(obj, "\"current\":");
                    liveCh[idx].pf = extractJsonFloat(obj, "\"pf\":");
                    liveCh[idx].energy_wh = extractJsonFloat(obj, "\"day_energy\":") * 1000.0; // kWh to Wh
                    found = true;
                    Serial.print("  em:"); Serial.print(ch);
                    Serial.print(" P="); Serial.print(liveCh[idx].power_w, 1);
                    Serial.print("W V="); Serial.print(liveCh[idx].voltage_v, 1);
                    Serial.print("V I="); Serial.print(liveCh[idx].current_a, 3);
                    Serial.println("A");
                }
            }
        }
        if (found) Serial.println("Parsed Refoss RPC format OK!");
    }

    if (!found) {
        Serial.println("Could not parse energy data");
        return;
    }

    // Calculate totals — Bi-phase system
    // CT clamp assignments (confirmed from distribution panel):
    //   A1 (ch1) = Solar Phase A        A2 (ch4) = Grid Phase A
    //   B1 (ch2) = House Phase B        B2 (ch5) = Solar Phase B
    //   C1 (ch3) = Shower (House)       C2 (ch6) = Grid Phase B
    //
    // All three quantities are DIRECTLY MEASURED:
    //   Solar = A1 + |B2|   (B2 may read negative due to CT direction)
    //   Grid  = A2 + C2     (negative = exporting, positive = importing)
    //   House = |B1| + |C1| (absolute values, power always consumed)
    liveSolar = fabs(liveCh[0].power_w) + fabs(liveCh[4].power_w);   // |A1| + |B2| (abs for both — CT clamp direction independent)
    liveGrid  = liveCh[3].power_w + liveCh[5].power_w;          // A2 + C2 (direct grid measurement)
    liveHome  = fabs(liveCh[1].power_w) + fabs(liveCh[2].power_w); // |B1| + |C1| (house consumption)
    refossDataValid = true;
}

float extractJsonFloat(String json, String key) {
    int idx = json.indexOf(key);
    if (idx < 0) return 0;
    int start = idx + key.length();
    int end = json.indexOf(",", start);
    if (end < 0) end = json.indexOf("}", start);
    if (end < 0) return 0;
    return json.substring(start, end).toFloat();
}

unsigned long getUnixTimestamp() {
    time_t now = time(nullptr);
    if (now > 1000000000) return (unsigned long)now;
    return millis() / 1000;
}


// ==================== ACTUAL DEVICE STATE HELPERS ====================
// Query real hardware status using hashrate + power — not just TCP ping.
// Used by verifyMinerStates() every 5 min and at boot.

// Query Avalon Q via CGMiner API: returns actual mode based on hashrate + WORKMODE
// Sets: actualAvalonMode, actualAvalonMHS, actualAvalonMPO
void queryAvalonActualState() {
    actualAvalonMode = "off";
    actualAvalonMHS = 0;
    actualAvalonMPO = 0;
    
    // Step 1: Try TCP connect
    WiFiClient client;
    client.setTimeout(3000);
    if (!client.connect(miners[3].ip, miners[3].port)) {
        actualAvalonMode = "off";
        Serial.println("Verify Avalon: TCP unreachable → OFF");
        return;
    }
    
    // Step 2: Send "summary" to get hashrate
    client.print("summary");
    client.flush();
    delay(500);
    
    String resp = "";
    unsigned long start = millis();
    while ((millis() - start) < 5000) {
        while (client.available()) {
            char c = client.read();
            resp += c;
            if (c == '\0' || resp.length() > 2000) break;
        }
        if (resp.indexOf("SUMMARY") >= 0 && resp.length() > 50) break;
        delay(50);
    }
    client.stop();
    
    String mhsStr = extractEqualsValue(resp, "MHS av");
    if (mhsStr.length() > 0) {
        actualAvalonMHS = mhsStr.toFloat();
    }
    
    Serial.print("Verify Avalon: MHS av="); Serial.print(actualAvalonMHS, 0);
    
    // Step 3: Send "estats" to get WORKMODE and MPO (power)
    delay(200);
    WiFiClient client2;
    client2.setTimeout(3000);
    if (client2.connect(miners[3].ip, miners[3].port)) {
        client2.print("estats");
        client2.flush();
        delay(500);
        
        String resp2 = "";
        start = millis();
        while ((millis() - start) < 8000) {
            while (client2.available()) {
                char c = client2.read();
                resp2 += c;
                if (resp2.length() > 4000) break;
            }
            if (resp2.indexOf("STATS=") >= 0 && resp2.length() > 200) {
                delay(500);
                while (client2.available()) {
                    char c = client2.read();
                    resp2 += c;
                    if (resp2.length() > 4000) break;
                }
                break;
            }
            delay(50);
        }
        client2.stop();
        
        String mpo = extractBracketValue(resp2, "MPO");
        String wm = extractBracketValue(resp2, "WORKMODE");
        
        if (mpo.length() > 0) actualAvalonMPO = mpo.toInt();
        
        Serial.print(" MPO="); Serial.print(actualAvalonMPO);
        Serial.print(" WORKMODE="); Serial.print(wm);
        
        // Determine actual mode from hashrate + workmode
        // If hashrate < 1000 MH/s (1 GH/s), consider it sleeping/off
        if (actualAvalonMHS < 1000) {
            actualAvalonMode = "sleep";
            Serial.println(" → SLEEP (low hashrate)");
        } else if (wm == "0") {
            actualAvalonMode = "low";
            Serial.println(" → LOW");
        } else if (wm == "1") {
            actualAvalonMode = "mid";
            Serial.println(" → MID");
        } else if (wm == "2") {
            actualAvalonMode = "high";
            Serial.println(" → HIGH");
        } else {
            // WORKMODE not found but hashrate present — running in unknown mode
            actualAvalonMode = "low";  // default assumption
            Serial.println(" → RUNNING (mode unknown, assume low)");
        }
    } else {
        // Can't get estats but summary worked — it's reachable but mode unknown
        if (actualAvalonMHS < 1000) {
            actualAvalonMode = "sleep";
            Serial.println(" → SLEEP (estats failed, low hashrate)");
        } else {
            actualAvalonMode = "low";  // conservative assumption
            Serial.println(" → RUNNING (estats failed, assume low)");
        }
    }
}

// Query a BitAxe/Nerdaxe/Octaxe miner: check if actually hashing
// Sets: actualBitaxeHR[idx], actualBitaxePwr[idx], actualBitaxeOn[idx]
void queryBitaxeActualState(int idx) {
    actualBitaxeHR[idx] = 0;
    actualBitaxePwr[idx] = 0;
    actualBitaxeOn[idx] = false;
    
    HTTPClient http;
    String url = "http://" + String(miners[idx].ip) + "/api/system/info";
    http.begin(url);
    http.setTimeout(3000);
    int httpCode = http.GET();
    
    if (httpCode != 200) {
        http.end();
        Serial.print("Verify "); Serial.print(miners[idx].name);
        Serial.println(": unreachable → OFF");
        return;
    }
    
    String response = http.getString();
    http.end();
    
    String hr_s = extractJsonValue(response, "\"hashRate\":");
    String pwr_s = extractJsonValue(response, "\"power\":");
    
    if (hr_s.length() > 0) actualBitaxeHR[idx] = hr_s.toFloat();
    if (pwr_s.length() > 0) actualBitaxePwr[idx] = pwr_s.toFloat();
    
    // Consider "ON" if hashrate > 0 AND power > 1W
    actualBitaxeOn[idx] = (actualBitaxeHR[idx] > 0 && actualBitaxePwr[idx] > 1.0);
    
    Serial.print("Verify "); Serial.print(miners[idx].name);
    Serial.print(": HR="); Serial.print(actualBitaxeHR[idx], 0);
    Serial.print(" Pwr="); Serial.print(actualBitaxePwr[idx], 1);
    Serial.print("W → "); Serial.println(actualBitaxeOn[idx] ? "ON" : "OFF");
}

// Helper: compare expected avalon mode string with actual
// Returns true if they match (both off/sleep, or same active mode)
bool avalonModeMatches(const char* expected, String actual) {
    if (strcmp(expected, "off") == 0) {
        return (actual == "off" || actual == "sleep");
    }
    return (actual == String(expected));
}

// ==================== PRE-TRANSITION STATE VERIFICATION ====================
// Before each 5-min decision, verify actual device states match expected state.
// Queries actual hashrate + power from ALL miners (not just TCP ping).
// After power outages or relay failures, devices may be in unexpected states.
// If mismatch: force-correct devices and log a correction transition (old_id == new_id).

void verifyMinerStates() {
    if (!autoMiningEnabled) return;
    
    Serial.println("\n--- VERIFY: Querying actual device states ---");
    
    const MiningProfile &expected = profiles[miningState];
    bool mismatch = false;
    String mismatches = "";
    
    // 1. Check Tasmota relay states (actual relay position)
    bool actualR1 = false, actualR2 = false;
    bool tasmotaOk = false;
    {
        HTTPClient http;
        String url = "http://" + String(tasmotaIP) + "/cm?cmnd=Status%200";
        http.begin(url);
        http.setAuthorization(tasmotaUser, tasmotaPassword);
        http.setTimeout(3000);
        int httpCode = http.GET();
        
        if (httpCode == 200) {
            String resp = http.getString();
            http.end();
            tasmotaOk = true;
            
            actualR1 = (resp.indexOf("\"POWER1\":\"ON\"") >= 0);
            actualR2 = (resp.indexOf("\"POWER2\":\"ON\"") >= 0);
            
            Serial.print("Verify Tasmota: R1="); Serial.print(actualR1 ? "ON" : "OFF");
            Serial.print(" R2="); Serial.println(actualR2 ? "ON" : "OFF");
            
            if (actualR1 != expected.relay1) {
                mismatch = true;
                mismatches += " R1:" + String(actualR1 ? "ON" : "OFF") + "!=" + String(expected.relay1 ? "ON" : "OFF");
            }
            if (actualR2 != expected.relay2) {
                mismatch = true;
                mismatches += " R2:" + String(actualR2 ? "ON" : "OFF") + "!=" + String(expected.relay2 ? "ON" : "OFF");
            }
        } else {
            http.end();
            Serial.println("Verify: Tasmota unreachable (skip relay check)");
        }
    }
    
    // 2. Query BitAxe miners (idx 0,1,2) — actual hashrate + power
    for (int i = 0; i < 3; i++) {
        queryBitaxeActualState(i);
    }
    
    // Cross-check: R1 miners (BitAxe idx=0 + Nerdaxe idx=1)
    bool r1MinersHashing = (actualBitaxeOn[0] || actualBitaxeOn[1]);
    if (expected.relay1 && tasmotaOk && actualR1 && !r1MinersHashing) {
        // Relay is ON but miners aren't hashing — could be booting
        Serial.println("Verify: R1 ON but miners not hashing yet (may be booting)");
    }
    
    // Cross-check: R2 miner (Octaxe idx=2)
    if (expected.relay2 && tasmotaOk && actualR2 && !actualBitaxeOn[2]) {
        Serial.println("Verify: R2 ON but Octaxe not hashing yet (may be booting)");
    }
    
    // 3. Query Avalon Q — actual hashrate + workmode + power
    queryAvalonActualState();
    
    // Check Avalon mode mismatch
    if (!avalonModeMatches(expected.avalonMode, actualAvalonMode)) {
        mismatch = true;
        mismatches += " Avalon:" + actualAvalonMode + "!=" + String(expected.avalonMode);
    }
    
    // Build verify summary for UI
    lastVerifyResult = "R1:" + String(actualR1 ? "ON" : "OFF");
    lastVerifyResult += " R2:" + String(actualR2 ? "ON" : "OFF");
    lastVerifyResult += " AV:" + actualAvalonMode;
    if (actualAvalonMHS > 0) {
        if (actualAvalonMHS > 1000000) lastVerifyResult += "(" + String(actualAvalonMHS / 1e6, 1) + "TH)";
        else lastVerifyResult += "(" + String(actualAvalonMHS / 1e3, 0) + "GH)";
    }
    
    if (!mismatch) {
        Serial.println("Verify: ✓ All devices match expected S" + String(miningState) + " (" + String(expected.name) + ")");
        Serial.println("--- VERIFY COMPLETE (no action needed) ---\n");
        return;
    }
    
    // Mismatch detected — force-correct all devices
    Serial.println("\n!!! STATE MISMATCH DETECTED !!!");
    Serial.print("Expected: S"); Serial.print(miningState);
    Serial.print(" ("); Serial.print(expected.name); Serial.print(")");
    Serial.print(" Mismatches:"); Serial.println(mismatches);
    Serial.println("Force-correcting all devices...");
    
    // Force relay 1
    if (tasmotaOk && actualR1 != expected.relay1) {
        String r1res = controlTasmotaRelay(1, expected.relay1 ? "ON" : "OFF");
        Serial.print("  R1 forced "); Serial.print(expected.relay1 ? "ON" : "OFF");
        Serial.print(": "); Serial.println(r1res);
    }
    
    // Force relay 2
    if (tasmotaOk && actualR2 != expected.relay2) {
        String r2res = controlTasmotaRelay(2, expected.relay2 ? "ON" : "OFF");
        Serial.print("  R2 forced "); Serial.print(expected.relay2 ? "ON" : "OFF");
        Serial.print(": "); Serial.println(r2res);
    }
    
    // Force Avalon Q state — based on actual mode detection
    bool avalonExpectedOn = (strcmp(expected.avalonMode, "off") != 0);
    bool avalonActuallyOn = (actualAvalonMode != "off" && actualAvalonMode != "sleep");
    
    if (avalonExpectedOn && !avalonActuallyOn) {
        // Avalon should be mining but is sleeping/off
        if (actualAvalonMode == "off") {
            Serial.println("  Avalon Q: unreachable, attempting wake...");
            controlAvalon(3, "wakeup");
            delay(3000);
        } else if (actualAvalonMode == "sleep") {
            Serial.println("  Avalon Q: sleeping (CGMiner alive but not hashing), waking...");
            controlAvalon(3, "wakeup");
            delay(3000);
        }
        String r = controlAvalon(3, expected.avalonMode);
        Serial.print("  Avalon Q: set "); Serial.print(expected.avalonMode);
        Serial.print(" → "); Serial.println(r);
    } else if (!avalonExpectedOn && avalonActuallyOn) {
        // Avalon should be off but is mining
        Serial.print("  Avalon Q: running in "); Serial.print(actualAvalonMode);
        Serial.println(" but should be OFF, sending standby...");
        controlAvalon(3, "standby");
    } else if (avalonExpectedOn && avalonActuallyOn && actualAvalonMode != String(expected.avalonMode)) {
        // Avalon is mining but in wrong mode (e.g., low vs mid)
        Serial.print("  Avalon Q: mode "); Serial.print(actualAvalonMode);
        Serial.print(" → changing to "); Serial.println(expected.avalonMode);
        String r = controlAvalon(3, expected.avalonMode);
        Serial.print("  Result: "); Serial.println(r);
    }
    
    lastAction = "CORRECTED: " + String(expected.name) + mismatches;
    
    // Log correction transition (old_id == new_id signals a correction)
    float avgGrid = 0;
    for (int s = 0; s < aggCount; s++) avgGrid += aggGrid[s];
    if (aggCount > 0) avgGrid /= aggCount;
    
    String tj = "{\"old_id\":" + String(miningState) +
                ",\"new_id\":" + String(miningState) +
                ",\"surplus\":0" +
                ",\"grid\":" + String(avgGrid, 1) + "}";
    supabaseInsert("transitions", tj);
    
    Serial.println("Correction logged to Supabase (old_id == new_id)");
    Serial.println("!!! CORRECTION COMPLETE !!!\n");
    showWebUI();
}

// ==================== MINING DECISION ENGINE ====================

void runMiningDecision() {
    if (!autoMiningEnabled || !refossDataValid) return;

    // Use averaged grid from this 5-min window
    float avgGrid = 0;
    for (int s = 0; s < aggCount; s++) avgGrid += aggGrid[s];
    if (aggCount > 0) avgGrid /= aggCount;

    // surplus = how much power we can use for mining
    // grid negative = exporting = surplus available
    // Add back current mining power since it's already in the grid reading
    float surplus = -avgGrid + profiles[miningState].totalW;

    // Find the highest profile whose totalW fits UNDER the surplus
    int targetState = 0;
    for (int i = NUM_PROFILES - 1; i >= 0; i--) {
        if (profiles[i].totalW <= surplus) {
            targetState = i;
            break;
        }
    }

    Serial.println("\n=== MINING DECISION ===");
    Serial.print("Avg Grid: "); Serial.print(avgGrid, 0);
    Serial.print("W  Surplus: "); Serial.print(surplus, 0); Serial.println("W");
    Serial.print("Current: S"); Serial.print(miningState);
    Serial.print(" ("); Serial.print(profiles[miningState].name);
    Serial.print(")  Target: S"); Serial.print(targetState);
    Serial.print(" ("); Serial.print(profiles[targetState].name);
    Serial.println(")");

    if (targetState == miningState) {
        Serial.println("No change needed.");
        Serial.println("========================\n");
        return;
    }

    // Apply the new state + persist to NVS
    int oldState = miningState;
    miningState = targetState;
    prefs.begin("mining", false);
    prefs.putInt("state", miningState);
    prefs.end();
    Serial.print("NVS: saved miningState="); Serial.println(miningState);
    const MiningProfile &p = profiles[targetState];

    Serial.print(">>> SWITCHING to "); Serial.print(p.name);
    Serial.print(" ("); Serial.print(p.totalW); Serial.println("W)");

    // Control Relay 1 (BitAxe + Nerdaxe 81W)
    bool r1now = profiles[oldState].relay1;
    if (p.relay1 != r1now) {
        String r = controlTasmotaRelay(1, p.relay1 ? "ON" : "OFF");
        Serial.print("  R1 (BN 81W): "); Serial.println(r);
    }

    // Control Relay 2 (Octaxe 180W)
    bool r2now = profiles[oldState].relay2;
    if (p.relay2 != r2now) {
        String r = controlTasmotaRelay(2, p.relay2 ? "ON" : "OFF");
        Serial.print("  R2 (OCT 180W): "); Serial.println(r);
    }

    // Control Avalon Q mode — idx 3 in miners[]
    const char* avNow = profiles[oldState].avalonMode;
    if (strcmp(p.avalonMode, avNow) != 0) {
        if (strcmp(p.avalonMode, "off") == 0) {
            String r = controlAvalon(3, "standby");
            Serial.print("  Avalon Q: sleep → "); Serial.println(r);
        } else {
            // Wake first if was sleeping
            if (strcmp(avNow, "off") == 0) {
                String r = controlAvalon(3, "wakeup");
                Serial.print("  Avalon Q: wake → "); Serial.println(r);
                delay(2000);
            }
            String r = controlAvalon(3, p.avalonMode);
            Serial.print("  Avalon Q: "); Serial.print(p.avalonMode);
            Serial.print(" → "); Serial.println(r);
        }
    }

    lastAction = "Auto: " + String(p.name) + " (" + String(p.totalW) + "W)";

    // Log transition to Supabase
    String tj = "{\"old_id\":" + String(oldState) +
                ",\"new_id\":" + String(targetState) +
                ",\"surplus\":" + String(surplus, 1) +
                ",\"grid\":" + String(avgGrid, 1) + "}";
    supabaseInsert("transitions", tj);

    Serial.println("========================\n");
    showWebUI();
}

// ==================== SUPABASE REST API ====================

void pushToSupabase() {
    if (aggCount == 0) return;

    Serial.print("Pushing "); Serial.print(aggCount);
    Serial.println(" samples to Supabase...");

    // Calculate averages — only power and pf
    float avgPwr[6] = {0}, avgPf[6] = {0};
    float avgSol = 0, avgGrd = 0, avgHm = 0;

    for (int s = 0; s < aggCount; s++) {
        for (int i = 0; i < 6; i++) {
            avgPwr[i] += aggPower[i][s];
            avgPf[i]  += aggPf[i][s];
        }
        avgSol += aggSolar[s];
        avgGrd += aggGrid[s];
        avgHm  += aggHome[s];
    }
    for (int i = 0; i < 6; i++) {
        avgPwr[i] /= aggCount;
        avgPf[i]  /= aggCount;
    }
    avgSol /= aggCount;
    avgGrd /= aggCount;
    avgHm  /= aggCount;

    // Minimal JSON for "energy" table
    String json = "{";
    json += "\"a1_w\":" + String(avgPwr[0], 1) + ",\"a1_pf\":" + String(avgPf[0], 2) + ",";
    json += "\"a2_w\":" + String(avgPwr[3], 1) + ",\"a2_pf\":" + String(avgPf[3], 2) + ",";
    json += "\"b1_w\":" + String(avgPwr[1], 1) + ",\"b1_pf\":" + String(avgPf[1], 2) + ",";
    json += "\"b2_w\":" + String(avgPwr[4], 1) + ",\"b2_pf\":" + String(avgPf[4], 2) + ",";
    json += "\"c1_w\":" + String(avgPwr[2], 1) + ",\"c1_pf\":" + String(avgPf[2], 2) + ",";
    json += "\"c2_w\":" + String(avgPwr[5], 1) + ",\"c2_pf\":" + String(avgPf[5], 2) + ",";
    json += "\"solar\":" + String(avgSol, 1) + ",";
    json += "\"grid\":" + String(avgGrd, 1) + ",";
    json += "\"home\":" + String(avgHm, 1) + ",";
    json += "\"n\":" + String(aggCount);
    json += "}";

    bool ok = supabaseInsert("energy", json);
    Serial.println(ok ? "Supabase: OK" : "Supabase: FAILED");

    aggCount = 0;
    showWebUI();
}

bool supabaseInsert(String table, String jsonBody) {
    WiFiClientSecure client;
    client.setInsecure(); // skip TLS cert verification (free tier)

    HTTPClient https;
    String url = String(SUPABASE_URL) + "/rest/v1/" + table;

    if (!https.begin(client, url)) {
        Serial.println("Supabase: connection failed");
        return false;
    }

    https.addHeader("Content-Type", "application/json");
    https.addHeader("apikey", SUPABASE_APIKEY);
    https.addHeader("Authorization", "Bearer " + String(SUPABASE_APIKEY));
    https.addHeader("Prefer", "return=minimal");
    https.setTimeout(15000);

    int code = https.POST(jsonBody);
    String resp = https.getString();
    https.end();

    Serial.print("Supabase POST "); Serial.print(table);
    Serial.print(" -> "); Serial.print(code);
    if (code != 201 && code != 200) {
        Serial.print(" ERR: "); Serial.println(resp);
        return false;
    }
    Serial.println(" OK");
    return true;
}


// ==================== RELAY CONTROL ====================

String controlTasmotaRelay(int relayNum, String action) {
    HTTPClient http;
    String cmd = "Power" + String(relayNum) + "%20" + action;
    String url = "http://" + String(tasmotaIP) + "/cm?cmnd=" + cmd;

    Serial.print("Tasmota: "); Serial.println(url);
    http.begin(url);
    http.setAuthorization(tasmotaUser, tasmotaPassword);

    int httpCode = http.GET();
    if (httpCode != 200) { http.end(); return "Failed HTTP " + String(httpCode); }

    String response = http.getString();
    http.end();

    if (response.indexOf("\"POWER" + String(relayNum) + "\":\"ON\"") >= 0)
        return "Relay " + String(relayNum) + " ON";
    if (response.indexOf("\"POWER" + String(relayNum) + "\":\"OFF\"") >= 0)
        return "Relay " + String(relayNum) + " OFF";
    if (response.indexOf("POWER") >= 0) return "Command sent";
    return "Response: " + response;
}

String getTasmotaRelayStatus() {
    HTTPClient http;
    String url = "http://" + String(tasmotaIP) + "/cm?cmnd=Status%200";
    http.begin(url);
    http.setAuthorization(tasmotaUser, tasmotaPassword);

    int httpCode = http.GET();
    if (httpCode != 200) { http.end(); return "{\"error\":\"HTTP " + String(httpCode) + "\"}"; }

    String response = http.getString();
    http.end();

    String p1 = "Unknown", p2 = "Unknown";
    if (response.indexOf("\"POWER1\":\"ON\"") >= 0) p1 = "ON";
    else if (response.indexOf("\"POWER1\":\"OFF\"") >= 0) p1 = "OFF";
    if (response.indexOf("\"POWER2\":\"ON\"") >= 0) p2 = "ON";
    else if (response.indexOf("\"POWER2\":\"OFF\"") >= 0) p2 = "OFF";

    String uptime = "N/A";
    int i1 = response.indexOf("\"Uptime\":\"");
    if (i1 > 0) { int i2 = response.indexOf("\"", i1 + 10); uptime = response.substring(i1 + 10, i2); }

    String rssi = "N/A";
    i1 = response.indexOf("\"Signal\":");
    if (i1 > 0) { int i2 = response.indexOf(",", i1); rssi = response.substring(i1 + 9, i2) + "%"; }

    return "{\"power1\":\"" + p1 + "\",\"power2\":\"" + p2 +
           "\",\"uptime\":\"" + uptime + "\",\"rssi\":\"" + rssi + "\"}";
}

// ==================== MINER CONTROL ====================

String restartBitAxe(int idx) {
    HTTPClient http;
    String endpoints[] = {
        "http://" + String(miners[idx].ip) + "/api/system/restart",
        "http://" + String(miners[idx].ip) + "/restart",
        "http://" + String(miners[idx].ip) + "/api/restart"
    };
    for (int i = 0; i < 3; i++) {
        http.begin(endpoints[i]);
        if (http.POST("") == 200) { http.end(); return String(miners[idx].name) + " restarting!"; }
        http.end();
        http.begin(endpoints[i]);
        if (http.GET() == 200) { http.end(); return String(miners[idx].name) + " restarting!"; }
        http.end();
    }
    return "Failed to restart";
}

// Helper: extract a clean numeric string from JSON (no control chars)
String extractJsonValue(String json, String key) {
    int idx = json.indexOf(key);
    if (idx < 0) return "";
    int start = idx + key.length();
    // Skip whitespace
    while (start < (int)json.length() && (json[start] == ' ' || json[start] == '\t')) start++;
    int end = json.indexOf(",", start);
    if (end < 0) end = json.indexOf("}", start);
    if (end < 0) return "";
    String val = json.substring(start, end);
    val.trim();
    // Remove surrounding quotes if present
    if (val.startsWith("\"")) val = val.substring(1);
    if (val.endsWith("\"")) val = val.substring(0, val.length() - 1);
    return val;
}

String getBitAxeStatus(int idx) {
    HTTPClient http;
    String url = "http://" + String(miners[idx].ip) + "/api/system/info";
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode != 200) { http.end(); return "{\"error\":\"HTTP " + String(httpCode) + "\"}"; }

    String response = http.getString();
    http.end();

    String json = "{";

    // hashRate
    String hr_s = extractJsonValue(response, "\"hashRate\":");
    if (hr_s.length() > 0) {
        float hr = hr_s.toFloat();
        if (hr > 1000000000) json += "\"hashrate\":\"" + String(hr / 1e9, 2) + " GH/s\",";
        else if (hr > 1000000) json += "\"hashrate\":\"" + String(hr / 1e6, 2) + " MH/s\",";
        else json += "\"hashrate\":\"" + String(hr, 0) + " H/s\",";
    } else json += "\"hashrate\":\"N/A\",";

    // temp
    String temp_s = extractJsonValue(response, "\"temp\":");
    json += "\"temp\":\"" + (temp_s.length() > 0 ? temp_s : "N/A") + "\",";

    // power
    String pwr_s = extractJsonValue(response, "\"power\":");
    json += "\"power\":\"" + (pwr_s.length() > 0 ? pwr_s : "N/A") + "\",";

    // bestDiff
    String bd_s = extractJsonValue(response, "\"bestDiff\":");
    json += "\"bestDiff\":\"" + (bd_s.length() > 0 ? bd_s : "N/A") + "\"";

    json += "}";
    return json;
}


// ==================== AVALON Q CONTROL ====================

// Helper: extract value from CGMiner bracket format: KEY[VALUE]
String extractBracketValue(String resp, String key) {
    String search = key + "[";
    int i1 = resp.indexOf(search);
    if (i1 < 0) return "";
    int vs = i1 + search.length();
    int ve = resp.indexOf("]", vs);
    if (ve < 0) return "";
    return resp.substring(vs, ve);
}

// Helper: extract value from CGMiner equals format: KEY=VALUE,
String extractEqualsValue(String resp, String key) {
    String search = key + "=";
    int i1 = resp.indexOf(search);
    if (i1 < 0) return "";
    int vs = i1 + search.length();
    int ve = resp.indexOf(",", vs);
    if (ve < 0) ve = resp.indexOf("|", vs);
    if (ve < 0) ve = resp.length();
    return resp.substring(vs, ve);
}

String getAvalonStatus(int idx) {
    String json = "{";
    
    // Step 1: Send "summary" command for hash rate and uptime
    {
        WiFiClient client;
        if (!client.connect(miners[idx].ip, miners[idx].port)) 
            return "{\"error\":\"Connection failed\"}";
        
        client.print("summary");
        client.flush();
        delay(500);
        
        String resp = "";
        unsigned long start = millis();
        while ((millis() - start) < 5000) {
            while (client.available()) {
                char c = client.read();
                resp += c;
                if (c == '\0' || resp.length() > 2000) break;
            }
            if (resp.indexOf("SUMMARY") >= 0 && resp.length() > 50) break;
            delay(50);
        }
        client.stop();
        
        Serial.print("Avalon summary ("); Serial.print(resp.length()); Serial.println("b)");
        
        // Parse summary format: MHS av=50391540.54, Elapsed=1272
        String mhsStr = extractEqualsValue(resp, "MHS av");
        if (mhsStr.length() > 0) {
            float mhs = mhsStr.toFloat();
            if (mhs > 1000000) json += "\"hashrate\":\"" + String(mhs / 1e6, 2) + " TH/s\",";
            else if (mhs > 1000) json += "\"hashrate\":\"" + String(mhs / 1e3, 2) + " GH/s\",";
            else json += "\"hashrate\":\"" + String(mhs, 0) + " MH/s\",";
        } else json += "\"hashrate\":\"N/A\",";
        
        String elapsed = extractEqualsValue(resp, "Elapsed");
        if (elapsed.length() > 0) {
            long secs = elapsed.toInt();
            long hrs = secs / 3600;
            long mins = (secs % 3600) / 60;
            json += "\"uptime\":\"" + String(hrs) + "h" + String(mins) + "m\",";
        } else json += "\"uptime\":\"N/A\",";
    }
    
    // Step 2: Send "estats" command for temp, power, workmode
    delay(200);
    {
        WiFiClient client;
        if (client.connect(miners[idx].ip, miners[idx].port)) {
            client.print("estats");
            client.flush();
            delay(500);
            
            String resp = "";
            unsigned long start = millis();
            while ((millis() - start) < 8000) {
                while (client.available()) {
                    char c = client.read();
                    resp += c;
                    if (resp.length() > 4000) break;
                }
                if (resp.indexOf("STATS=") >= 0 && resp.length() > 200) {
                    // Wait a bit more for full response
                    delay(500);
                    while (client.available()) {
                        char c = client.read();
                        resp += c;
                        if (resp.length() > 4000) break;
                    }
                    break;
                }
                delay(50);
            }
            client.stop();
            
            Serial.print("Avalon estats ("); Serial.print(resp.length()); Serial.println("b)");
            
            // Parse bracket format from estats response
            // Temp: ITemp[36] (internal), TMax[70], TAvg[65]
            String itemp = extractBracketValue(resp, "ITemp");
            String tavg = extractBracketValue(resp, "TAvg");
            if (tavg.length() > 0) json += "\"temp\":\"" + tavg + " (avg) / " + itemp + " (int)\",";
            else if (itemp.length() > 0) json += "\"temp\":\"" + itemp + "\",";
            else json += "\"temp\":\"N/A\",";
            
            // Power: MPO[800] = Mode Power Output in watts
            String mpo = extractBracketValue(resp, "MPO");
            if (mpo.length() > 0) json += "\"power\":\"" + mpo + "\",";
            else json += "\"power\":\"N/A\",";
            
            // Work mode: WORKMODE[0] (0=low, 1=mid, 2=high)
            String wm = extractBracketValue(resp, "WORKMODE");
            if (wm.length() > 0) {
                String modeName = "Unknown";
                if (wm == "0") modeName = "Low (0)";
                else if (wm == "1") modeName = "Mid (1)";
                else if (wm == "2") modeName = "High (2)";
                else modeName = wm;
                json += "\"mode\":\"" + modeName + "\"";
            } else json += "\"mode\":\"N/A\"";
        } else {
            json += "\"temp\":\"N/A\",\"power\":\"N/A\",\"mode\":\"N/A\"";
        }
    }
    
    json += "}";
    return json;
}

String controlAvalon(int idx, String command) {
    WiFiClient client;
    if (!client.connect(miners[idx].ip, miners[idx].port)) return "Connection failed";

    String cmd = "";
    if (command == "summary") cmd = "stats";
    else if (command == "standby") {
        unsigned long ts = getUnixTimestamp() + 60;
        cmd = "ascset|0,softoff,1: " + String(ts);
    } else if (command == "wakeup") {
        unsigned long ts = getUnixTimestamp() + 60;
        cmd = "ascset|0,softon,1: " + String(ts);
    } else if (command == "low")    cmd = "ascset|0,workmode,set,0";
    else if (command == "mid")      cmd = "ascset|0,workmode,set,1";
    else if (command == "high")     cmd = "ascset|0,workmode,set,2";
    else if (command == "reboot")   cmd = "ascset|0,reboot,0";

    Serial.print("Avalon cmd: "); Serial.println(cmd);
    client.print(cmd);
    client.flush();
    delay(1000);

    String response = "";
    unsigned long start = millis();
    while ((millis() - start) < 5000) {
        while (client.available()) {
            char c = client.read(); response += c;
            if (c == '\0' || response.length() > 2000) break;
        }
        if (response.indexOf("STATUS=") >= 0) break;
        delay(50);
    }
    client.stop();

    if (response.length() > 0) {
        if (response.indexOf("ASC 0 set OK") >= 0) return "Success! Mode changed.";
        if (response.indexOf("success softoff") >= 0) return "Entering standby...";
        if (response.indexOf("success softon") >= 0) return "Waking up!";
        if (response.indexOf("STATUS=S") >= 0) return "Command successful";
        if (response.indexOf("STATUS=E") >= 0) return "Error: Invalid command";
        return response.substring(0, min(150, (int)response.length()));
    }
    return "No response from miner";
}


// ==================== WEB SERVER HANDLERS ====================

void handleRestart() {
    if (!server.hasArg("miner")) return server.send(400, "text/plain", "Missing parameter");
    int idx = server.arg("miner").toInt();
    String mode = server.hasArg("mode") ? server.arg("mode") : "";

    String result;
    if (mode == "standby") {
        HTTPClient http;
        http.begin("http://" + String(miners[idx].ip) + "/api/system/restart");
        http.addHeader("Content-Type", "application/json");
        int code = http.POST("{\"mode\":\"standby\"}");
        result = (code == 200) ? "Standby sent" : "Failed HTTP " + String(code);
        http.end();
    } else {
        result = restartBitAxe(idx);
    }
    lastAction = String(miners[idx].name) + " " + (mode.length() ? mode : "restart");
    showWebUI();
    server.send(200, "text/plain", result);
}

void handleStatus() {
    if (!server.hasArg("miner")) return server.send(400, "application/json", "{\"error\":\"Missing\"}");
    int idx = server.arg("miner").toInt();
    String result;
    if (strcmp(miners[idx].type, "bitaxe") == 0) result = getBitAxeStatus(idx);
    else if (strcmp(miners[idx].type, "avalon") == 0) result = getAvalonStatus(idx);
    else result = "{\"error\":\"Unknown type\"}";
    lastAction = String(miners[idx].name) + " status";
    showWebUI();
    server.send(200, "application/json", result);
}

void handleAvalon() {
    if (!server.hasArg("miner") || !server.hasArg("cmd"))
        return server.send(400, "text/plain", "Missing parameters");
    int idx = server.arg("miner").toInt();
    String cmd = server.arg("cmd");
    String result = controlAvalon(idx, cmd);
    lastAction = "Avalon " + cmd;
    showWebUI();
    server.send(200, "text/plain", result);
}

void handleRelay() {
    if (!server.hasArg("relay")) return server.send(400, "text/plain", "Missing parameter");
    int relayNum = server.arg("relay").toInt();
    String action = server.hasArg("action") ? server.arg("action") : "TOGGLE";
    String result = controlTasmotaRelay(relayNum, action);
    lastAction = "Relay " + String(relayNum) + " " + action;
    showWebUI();
    server.send(200, "text/plain", result);
}

void handleRelayStatus() {
    server.send(200, "application/json", getTasmotaRelayStatus());
}

void handleEnergy() {
    String json = "{";
    json += "\"solar_w\":" + String(liveSolar, 1) + ",";
    json += "\"grid_w\":" + String(liveGrid, 1) + ",";
    json += "\"home_w\":" + String(liveHome, 1) + ",";
    json += "\"refoss_found\":" + String(refossFound ? "true" : "false") + ",";
    json += "\"refoss_ip\":\"" + refossIP + "\",";
    json += "\"samples\":" + String(aggCount) + ",";
    json += "\"mining_state\":" + String(miningState) + ",";
    json += "\"mining_name\":\"" + String(profiles[miningState].name) + "\",";
    json += "\"mining_w\":" + String(profiles[miningState].totalW) + ",";
    json += "\"auto_enabled\":" + String(autoMiningEnabled ? "true" : "false") + ",";
    json += "\"verify\":\"" + lastVerifyResult + "\",";
    json += "\"avalon_actual\":\"" + actualAvalonMode + "\",";
    json += "\"avalon_mhs\":" + String(actualAvalonMHS, 0) + ",";
    json += "\"avalon_mpo\":" + String(actualAvalonMPO) + ",";
    json += "\"channels\":[";
    const char* chNames[] = {"A1_Solar","B1_House","C1_Shower","A2_Grid","B2_Solar","C2_Grid"};
    for (int i = 0; i < 6; i++) {
        if (i > 0) json += ",";
        json += "{\"name\":\"" + String(chNames[i]) + "\",";
        json += "\"w\":" + String(liveCh[i].power_w, 1) + ",";
        json += "\"pf\":" + String(liveCh[i].pf, 2) + "}";
    }
    json += "]}";
    server.send(200, "application/json", json);
}

void handleSupabaseSync() {
    pushToSupabase();
    server.send(200, "text/plain", "Synced to Supabase (" + String(aggCount) + " samples)");
}

void handleNotFound() { server.send(404, "text/plain", "Not found"); }


// ==================== WEB UI ====================

void handleRoot() {
    String h = "<!DOCTYPE html><html><head>";
    h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    h += "<title>Solar Mining Control</title><style>";
    h += "*{margin:0;padding:0;box-sizing:border-box;}";
    h += "body{font-family:'Segoe UI',Arial,sans-serif;background:linear-gradient(135deg,#0f2027,#203a43,#2c5364);min-height:100vh;padding:15px;color:#fff;}";
    h += ".ctr{max-width:1200px;margin:0 auto;} h1{text-align:center;font-size:2.2em;margin-bottom:5px;}";
    h += ".sub{text-align:center;opacity:.8;margin-bottom:20px;}";
    h += ".card{background:rgba(255,255,255,.08);backdrop-filter:blur(10px);border-radius:12px;padding:18px;border:1px solid rgba(255,255,255,.15);margin-bottom:15px;}";
    h += ".ct{font-size:1.5em;margin-bottom:6px;font-weight:600;} .ci{opacity:.7;margin-bottom:4px;font-size:.9em;}";
    h += ".stats{background:rgba(0,0,0,.25);padding:12px;border-radius:8px;margin:10px 0;display:none;} .stats div{margin:3px 0;}";
    h += "button{background:linear-gradient(135deg,#667eea,#764ba2);color:#fff;border:none;padding:12px 18px;font-size:.95em;border-radius:8px;cursor:pointer;width:100%;margin:5px 0;font-weight:600;}";
    h += ".brow{display:flex;gap:6px;}.brow button{flex:1;}";
    h += ".bon{background:linear-gradient(135deg,#00b09b,#96c93d);}";
    h += ".boff{background:linear-gradient(135deg,#ff416c,#ff4b2b);}";
    h += ".btog{background:linear-gradient(135deg,#FFD700,#FF8C00);}";
    h += ".bstat{background:linear-gradient(135deg,#f093fb,#f5576c);}";
    h += ".blo{background:linear-gradient(135deg,#30cfd0,#330867);}";
    h += ".bmd{background:linear-gradient(135deg,#fa709a,#fee140);}";
    h += ".bhi{background:linear-gradient(135deg,#a8edea,#fed6e3);color:#333;}";
    h += "#result{position:fixed;top:15px;right:15px;max-width:340px;padding:15px;background:rgba(0,0,0,.92);border-radius:10px;display:none;z-index:1000;}";
    h += ".ep{border:2px solid #00ff88;} .rg{border:2px solid #FFD700;}";
    h += ".egrid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin:10px 0;}";
    h += ".ev{text-align:center;padding:10px;background:rgba(0,0,0,.3);border-radius:8px;}";
    h += ".ev .n{font-size:1.8em;font-weight:700;} .ev .l{font-size:.75em;opacity:.7;}";
    h += ".sol{color:#FFD700;} .grc{color:#ff6b6b;} .hmc{color:#4ecdc4;}";
    h += ".chg{display:grid;grid-template-columns:repeat(3,1fr);gap:5px;margin-top:8px;}";
    h += ".chi{font-size:.8em;padding:6px;background:rgba(0,0,0,.2);border-radius:5px;text-align:center;}";
    h += ".mn{margin-top:10px;padding:10px;background:rgba(0,0,0,.2);border-radius:8px;}";
    h += ".mnt{font-size:1.1em;font-weight:600;}";
    h += "@media(max-width:768px){h1{font-size:1.6em;}}";
    h += "</style></head><body><div class='ctr'>";
    h += "<h1>&#9889; Solar Mining Control</h1>";
    h += "<div class='sub'>ESP32 @ " + espIP + "</div>";
    server.sendContent(h);
    sendEnergyPanel();
    sendRelayGroup1();
    sendRelayGroup2();
    sendAvalonPanel();
    sendSupabasePanel();
    sendJavaScript();
    server.sendContent("</div></body></html>");
    server.sendContent("");
}


void sendEnergyPanel() {
    String h = "<div class='card ep'>";
    h += "<div class='ct'>&#128268; Energy Monitor (EMO6P)</div>";
    h += "<div class='ci' id='refI'>Searching...</div>";
    h += "<div class='egrid'>";
    h += "<div class='ev sol'><div class='n' id='eS'>--</div><div class='l'>&#9728; Solar W</div></div>";
    h += "<div class='ev grc'><div class='n' id='eG'>--</div><div class='l'>&#9889; Grid W</div></div>";
    h += "<div class='ev hmc'><div class='n' id='eH'>--</div><div class='l'>&#127968; Home W</div></div>";
    h += "</div>";
    h += "<div id='chD' class='chg'></div>";
    h += "<div style='text-align:center;margin-top:8px;font-size:.8em;opacity:.6;'>";
    h += "<span id='eSmp'>0</span> samples | Next push: <span id='eNxt'>--</span></div>";
    h += "</div>";
    server.sendContent(h);
}

void sendRelayGroup1() {
    String h = "<div class='card rg'>";
    h += "<div style='display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;'>";
    h += "<div class='ct'>&#128268; R1 &mdash; BitAxe + Nerdaxe (81W)</div>";
    h += "<div id='r1s' style='font-size:1.1em;font-weight:600;'>--</div></div>";
    h += "<div class='brow'>";
    h += "<button class='bon' onclick='relayCmd(1,\"ON\")'>&#9989; ON</button>";
    h += "<button class='boff' onclick='relayCmd(1,\"OFF\")'>&#10060; OFF</button></div>";
    // BitAxe (idx 0)
    h += "<div class='mn'><div class='mnt'>&#128421; BitAxe Rafa 21W <span style='opacity:.6;font-size:.8em;'>192.168.1.21</span></div>";
    h += "<div id='stats0' class='stats'></div>";
    h += "<div class='brow'><button class='bstat' onclick='getStatus(0)'>&#128202; Status</button>";
    h += "<button onclick='restart(0)'>&#128260; Restart</button></div></div>";
    // Nerdaxe (idx 1)
    h += "<div class='mn'><div class='mnt'>&#128421; Nerdaxe 60W <span style='opacity:.6;font-size:.8em;'>192.168.1.28</span></div>";
    h += "<div id='stats1' class='stats'></div>";
    h += "<div class='brow'><button class='bstat' onclick='getStatus(1)'>&#128202; Status</button>";
    h += "<button onclick='restart(1)'>&#128260; Restart</button></div></div></div>";
    server.sendContent(h);
}

void sendRelayGroup2() {
    String h = "<div class='card rg'>";
    h += "<div style='display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;'>";
    h += "<div class='ct'>&#128268; R2 &mdash; Octaxe (180W)</div>";
    h += "<div id='r2s' style='font-size:1.1em;font-weight:600;'>--</div></div>";
    h += "<div class='brow'>";
    h += "<button class='bon' onclick='relayCmd(2,\"ON\")'>&#9989; ON</button>";
    h += "<button class='boff' onclick='relayCmd(2,\"OFF\")'>&#10060; OFF</button></div>";
    // Octaxe (idx 2)
    h += "<div class='mn'><div class='mnt'>&#128421; Octaxe 180W <span style='opacity:.6;font-size:.8em;'>192.168.1.37</span></div>";
    h += "<div id='stats2' class='stats'></div>";
    h += "<div class='brow'><button class='bstat' onclick='getStatus(2)'>&#128202; Status</button>";
    h += "<button onclick='restart(2)'>&#128260; Restart</button></div></div></div>";
    server.sendContent(h);
}

void sendAvalonPanel() {
    String h = "<div class='card' style='border:2px solid #4facfe;'>";
    h += "<div class='ct'>&#128268; Avalon Q (API Only)</div>";
    h += "<div class='ci'>192.168.1.51:4028 &bull; CGMiner &bull; No relay</div>";
    h += "<div id='stats3' class='stats'></div>";
    h += "<button class='bstat' onclick='getAvalonStatus(3)'>&#128202; Get Status</button>";
    h += "<div class='brow'><button class='blo' onclick='avalonCmd(3,\"standby\")'>&#128164; Sleep</button>";
    h += "<button style='background:linear-gradient(135deg,#FF6B6B,#FFE66D);' onclick='avalonCmd(3,\"wakeup\")'>&#9728; Wake</button></div>";
    h += "<div class='brow'><button class='blo' onclick='avalonCmd(3,\"low\")'>&#128012; Low</button>";
    h += "<button class='bmd' onclick='avalonCmd(3,\"mid\")'>&#9889; Mid</button>";
    h += "<button class='bhi' onclick='avalonCmd(3,\"high\")'>&#128640; High</button></div>";
    h += "<button onclick='avalonCmd(3,\"reboot\")'>&#128260; Reboot</button></div>";
    server.sendContent(h);
}

void sendSupabasePanel() {
    String h = "<div class='card' style='border:2px solid #00ff88;'>";
    h += "<div class='ct'>&#128202; Supabase Cloud DB</div>";
    h += "<div class='ci'>solar-cluster</div>";
    h += "<button onclick='syncSupa()' style='background:linear-gradient(135deg,#00ff88,#00b4d8);color:#000;'>&#128260; Sync Now</button>";
    h += "<div id='supS' style='margin-top:8px;font-size:.85em;opacity:.7;'>Waiting...</div></div>";
    h += "<div id='result'></div>";
    server.sendContent(h);
}


void sendJavaScript() {
    String j = "<script>";
    j += "function showR(t,c){var r=document.getElementById('result');r.innerHTML=t;r.style.display='block';r.style.borderLeft='4px solid '+c;setTimeout(()=>r.style.display='none',5000);}";

    // Miner status
    j += "function getStatus(i){showR('Getting status...','#00ffff');";
    j += "fetch('/status?miner='+i).then(r=>r.json()).then(d=>{";
    j += "var s=document.getElementById('stats'+i);s.style.display='block';";
    j += "s.innerHTML='<div><b>Hash:</b> '+d.hashrate+'</div>';";
    j += "s.innerHTML+='<div><b>Temp:</b> '+(d.temp||'N/A')+'&#176;C</div>';";
    j += "s.innerHTML+='<div><b>Power:</b> '+(d.power||'N/A')+'W</div>';";
    j += "s.innerHTML+='<div><b>Best:</b> '+(d.bestDiff||'N/A')+'</div>';";
    j += "showR('Updated!','#00ff00');";
    j += "}).catch(e=>showR('Error: '+e,'#ff0000'));}";
    server.sendContent(j);

    j = "function restart(i){showR('Restarting...','#ffd700');";
    j += "fetch('/restart?miner='+i).then(r=>r.text()).then(t=>showR(t,'#00ff00')).catch(e=>showR('Error','#ff0000'));}";

    j += "function standbyBitaxe(i){showR('Standby...','#ffd700');";
    j += "fetch('/restart?miner='+i+'&mode=standby').then(r=>r.text()).then(t=>showR(t,'#00ff00')).catch(e=>showR('Error','#ff0000'));}";

    // Avalon
    j += "function getAvalonStatus(i){showR('Getting status...','#00ffff');";
    j += "fetch('/status?miner='+i).then(r=>r.json()).then(d=>{";
    j += "var s=document.getElementById('stats'+i);s.style.display='block';";
    j += "s.innerHTML='<div><b>Hash:</b> '+d.hashrate+'</div>';";
    j += "s.innerHTML+='<div><b>Temp:</b> '+d.temp+'&#176;C</div>';";
    j += "s.innerHTML+='<div><b>Power:</b> '+d.power+'W</div>';";
    j += "s.innerHTML+='<div><b>Mode:</b> '+d.mode+'</div>';";
    j += "s.innerHTML+='<div><b>Uptime:</b> '+d.uptime+'</div>';";
    j += "showR('Updated!','#00ff00');";
    j += "}).catch(e=>showR('Error: '+e,'#ff0000'));}";

    j += "function avalonCmd(i,cmd){";
    j += "var m={'summary':'Getting...','standby':'Sleeping...','wakeup':'Waking...','low':'Low mode...','mid':'Mid mode...','high':'High mode...','reboot':'Rebooting...'};";
    j += "showR(m[cmd]||'Sending...','#ffd700');";
    j += "fetch('/avalon?miner='+i+'&cmd='+cmd).then(r=>r.text()).then(t=>showR(t,'#00ffff')).catch(e=>showR('Error: '+e,'#ff0000'));}";
    server.sendContent(j);

    // Relay commands
    j = "function relayCmd(n,a){showR('Relay '+n+' '+a+'...','#ffd700');";
    j += "fetch('/relay?relay='+n+'&action='+a).then(r=>r.text()).then(t=>{showR(t,'#00ff00');getRS();}).catch(e=>showR('Error','#ff0000'));}";

    // Relay status
    j += "function getRS(){fetch('/relaystatus').then(r=>r.json()).then(d=>{";
    j += "if(d.error){document.getElementById('r1s').innerHTML='offline';document.getElementById('r2s').innerHTML='offline';return;}";
    j += "var p1=d.power1||'--';var p2=d.power2||'--';";
    j += "var c1=p1=='ON'?'#00ff00':'#ff4444';";
    j += "var c2=p2=='ON'?'#00ff00':'#ff4444';";
    j += "document.getElementById('r1s').innerHTML='<span style=\"color:'+c1+'\">'+p1+'</span>';";
    j += "document.getElementById('r2s').innerHTML='<span style=\"color:'+c2+'\">'+p2+'</span>';";
    j += "}).catch(e=>{document.getElementById('r1s').innerHTML='offline';document.getElementById('r2s').innerHTML='offline';});}";
    server.sendContent(j);

    // Energy + mining state polling
    j = "function getE(){fetch('/energy').then(r=>r.json()).then(d=>{";
    j += "document.getElementById('eS').textContent=Math.round(d.solar_w);";
    j += "document.getElementById('eG').textContent=Math.round(d.grid_w);";
    j += "document.getElementById('eH').textContent=Math.round(d.home_w);";
    j += "document.getElementById('eSmp').textContent=d.samples;";
    j += "var info=d.refoss_found?'EMO6P @ '+d.refoss_ip:'Searching...';";
    j += "info+=' | Mining: '+d.mining_name+' ('+d.mining_w+'W)';";
    j += "if(d.avalon_actual&&d.avalon_actual!='unknown') info+=' | AV:'+d.avalon_actual.toUpperCase();";
    j += "if(d.avalon_mpo>0) info+=' '+d.avalon_mpo+'W';";
    j += "document.getElementById('refI').textContent=info;";
    j += "var cd=document.getElementById('chD');cd.innerHTML='';";
    j += "var colors=['#FFD700','#ff6b6b','#87CEEB','#4ecdc4','#FFD700','#4ecdc4'];";
    j += "d.channels.forEach(function(c,i){";
    j += "cd.innerHTML+='<div class=\"chi\" style=\"border-left:3px solid '+colors[i]+'\"><b>'+c.name+'</b><br>'+c.w.toFixed(1)+'W<br>pf:'+c.pf.toFixed(2)+'</div>';";
    j += "});}).catch(e=>{});}";

    // Supabase sync
    j += "function syncSupa(){showR('Syncing...','#00ff88');";
    j += "fetch('/supabase').then(r=>r.text()).then(t=>{showR(t,'#00ff88');document.getElementById('supS').textContent='Last sync: just now';}).catch(e=>showR('Error','#ff0000'));}";

    // Auto-refresh
    j += "setTimeout(getRS,1000);";
    j += "setTimeout(getE,1500);";
    j += "setInterval(getRS,15000);";
    j += "setInterval(getE,10000);";
    j += "</script>";
    server.sendContent(j);
}

