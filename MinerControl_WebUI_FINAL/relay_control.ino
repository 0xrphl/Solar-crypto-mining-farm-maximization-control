/*
 * relay_control.ino — Tasmota relay + BitAxe/Nerdaxe/Octaxe + Avalon Q control
 */

// ==================== TASMOTA RELAY CONTROL ====================

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

// ==================== BITAXE / NERDAXE / OCTAXE ====================

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

String getBitAxeStatus(int idx) {
    HTTPClient http;
    String url = "http://" + String(miners[idx].ip) + "/api/system/info";
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode != 200) { http.end(); return "{\"error\":\"HTTP " + String(httpCode) + "\"}"; }

    String response = http.getString();
    http.end();

    String json = "{";

    String hr_s = extractJsonValue(response, "\"hashRate\":");
    if (hr_s.length() > 0) {
        float hr = hr_s.toFloat();
        if (hr > 1000000000) json += "\"hashrate\":\"" + String(hr / 1e9, 2) + " GH/s\",";
        else if (hr > 1000000) json += "\"hashrate\":\"" + String(hr / 1e6, 2) + " MH/s\",";
        else json += "\"hashrate\":\"" + String(hr, 0) + " H/s\",";
    } else json += "\"hashrate\":\"N/A\",";

    String temp_s = extractJsonValue(response, "\"temp\":");
    json += "\"temp\":\"" + (temp_s.length() > 0 ? temp_s : "N/A") + "\",";

    String pwr_s = extractJsonValue(response, "\"power\":");
    json += "\"power\":\"" + (pwr_s.length() > 0 ? pwr_s : "N/A") + "\",";

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
    float summaryMHS = 0;
    
    // Step 1: "summary" command
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
        
        String mhsStr = extractEqualsValue(resp, "MHS av");
        if (mhsStr.length() > 0) {
            summaryMHS = mhsStr.toFloat();
            if (summaryMHS > 1000000) json += "\"hashrate\":\"" + String(summaryMHS / 1e6, 2) + " TH/s\",";
            else if (summaryMHS > 1000) json += "\"hashrate\":\"" + String(summaryMHS / 1e3, 2) + " GH/s\",";
            else if (summaryMHS > 0) json += "\"hashrate\":\"" + String(summaryMHS, 0) + " MH/s\",";
            else json += "\"hashrate\":\"0 (idle)\",";
        } else json += "\"hashrate\":\"N/A\",";
        
        String elapsed = extractEqualsValue(resp, "Elapsed");
        if (elapsed.length() > 0) {
            long secs = elapsed.toInt();
            long hrs = secs / 3600;
            long mins = (secs % 3600) / 60;
            json += "\"uptime\":\"" + String(hrs) + "h" + String(mins) + "m\",";
        } else json += "\"uptime\":\"N/A\",";
    }
    
    // Step 2: "estats" command
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
            
            String softoff = extractBracketValue(resp, "SoftOFF");
            String state = extractBracketValue(resp, "STATE");
            String ghsavg = extractBracketValue(resp, "GHSavg");
            
            float ghsVal = 0;
            if (ghsavg.length() > 0) ghsVal = ghsavg.toFloat();
            
            int softoffVal = softoff.toInt();
            int stateVal = state.toInt();
            bool activelyHashing = (stateVal == 1 && ghsVal > 100.0);
            bool isStandby = false;
            if (softoffVal > 0 && !activelyHashing) isStandby = true;
            else if (stateVal != 1 && ghsVal < 1.0) isStandby = true;
            else if (summaryMHS < 1000 && ghsVal < 1.0) isStandby = true;
            
            Serial.print("  SoftOFF="); Serial.print(softoff);
            Serial.print(" STATE="); Serial.print(state);
            Serial.print(" GHSavg="); Serial.print(ghsavg);
            Serial.print(" → standby="); Serial.println(isStandby ? "YES" : "NO");
            
            String itemp = extractBracketValue(resp, "ITemp");
            String tavg = extractBracketValue(resp, "TAvg");
            if (tavg.length() > 0) json += "\"temp\":\"" + tavg + " (avg) / " + itemp + " (int)\",";
            else if (itemp.length() > 0) json += "\"temp\":\"" + itemp + "\",";
            else json += "\"temp\":\"N/A\",";
            
            String mpo = extractBracketValue(resp, "MPO");
            if (isStandby) {
                json += "\"power\":\"0 (standby, rated " + mpo + ")\",";
            } else if (mpo.length() > 0) {
                json += "\"power\":\"" + mpo + "\",";
            } else {
                json += "\"power\":\"N/A\",";
            }
            
            if (isStandby) {
                String wm = extractBracketValue(resp, "WORKMODE");
                String wmLabel = "";
                if (wm == "0") wmLabel = "Low";
                else if (wm == "1") wmLabel = "Mid";
                else if (wm == "2") wmLabel = "High";
                json += "\"mode\":\"STANDBY (was " + wmLabel + ")\"";
            } else {
                String wm = extractBracketValue(resp, "WORKMODE");
                if (wm.length() > 0) {
                    String modeName = "Unknown";
                    if (wm == "0") modeName = "Low (0)";
                    else if (wm == "1") modeName = "Mid (1)";
                    else if (wm == "2") modeName = "High (2)";
                    else modeName = wm;
                    json += "\"mode\":\"" + modeName + "\"";
                } else json += "\"mode\":\"N/A\"";
            }
        } else {
            json += "\"temp\":\"N/A\",\"power\":\"N/A\",\"mode\":\"N/A\"";
        }
    }
    
    json += "}";
    return json;
}

String controlAvalon(int idx, String command) {
    // Cooldown guard
    unsigned long now = millis();
    if (lastAvalonCmdTime > 0 && (now - lastAvalonCmdTime) < AVALON_CMD_COOLDOWN_MS) {
        if (command != "summary") {
            unsigned long remaining = (AVALON_CMD_COOLDOWN_MS - (now - lastAvalonCmdTime)) / 1000;
            Serial.print("Avalon COOLDOWN: skipping '"); Serial.print(command);
            Serial.print("' ("); Serial.print(remaining); Serial.println("s remaining)");
            return "Cooldown: wait " + String(remaining) + "s";
        }
    }

    // Skip redundant standby
    if (command == "standby" && (actualAvalonMode == "sleep" || actualAvalonMode == "off")) {
        Serial.println("Avalon: already sleeping/off, skipping redundant standby");
        return "Already in standby";
    }
    
    // Skip redundant wakeup
    if (command == "wakeup" && actualAvalonMode != "sleep" && actualAvalonMode != "off" && actualAvalonMode != "unknown") {
        Serial.println("Avalon: already active, skipping redundant wakeup");
        return "Already active";
    }

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
    
    if (command != "summary") {
        lastAvalonCmdTime = millis();
    }
    
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

// ==================== DEFERRED AVALON WORKMODE ====================
// Called every 30s from loop() after Refoss poll.
// After wakeup, the Avalon Q needs 30-60s to boot before accepting workmode.
// This retries the workmode command up to 5 times (every 30s = ~2.5 min window).

void checkPendingAvalonMode() {
    if (pendingAvalonMode.length() == 0) return;  // nothing pending

    unsigned long elapsed = millis() - pendingAvalonSetTime;
    pendingAvalonRetries++;

    Serial.print("\n--- DEFERRED AVALON: attempt "); Serial.print(pendingAvalonRetries);
    Serial.print("/5, mode='"); Serial.print(pendingAvalonMode);
    Serial.print("', elapsed="); Serial.print(elapsed / 1000);
    Serial.println("s since wakeup ---");

    // First attempt: wait at least 30s after wakeup for boot
    if (elapsed < 25000) {
        Serial.println("  Too soon after wakeup, waiting for next cycle...");
        pendingAvalonRetries--;  // don't count this attempt
        return;
    }

    // Reset cooldown so command goes through
    lastAvalonCmdTime = 0;

    // Try to set the workmode
    String result = controlAvalon(3, pendingAvalonMode);
    Serial.print("  Avalon workmode '"); Serial.print(pendingAvalonMode);
    Serial.print("' → "); Serial.println(result);

    // Check if it succeeded
    if (result.indexOf("Success") >= 0 || result.indexOf("successful") >= 0 || result.indexOf("Mode changed") >= 0) {
        Serial.println("  ✓ Deferred workmode ACCEPTED! Clearing pending.");
        lastAction = "Avalon set " + pendingAvalonMode + " (deferred)";
        pendingAvalonMode = "";
        pendingAvalonSetTime = 0;
        pendingAvalonRetries = 0;
        return;
    }

    // Give up after 5 retries (~2.5 min)
    if (pendingAvalonRetries >= 5) {
        Serial.println("  ✗ Deferred workmode FAILED after 5 retries. Giving up.");
        Serial.println("    Avalon may be in default mode (high). Will be caught by next verify cycle.");
        lastAction = "WARN: Avalon mode set failed (" + pendingAvalonMode + ")";
        pendingAvalonMode = "";
        pendingAvalonSetTime = 0;
        pendingAvalonRetries = 0;
        return;
    }

    Serial.println("  Will retry on next poll cycle (30s)...");
}
