/*
 * refoss.ino — Refoss EMO6P discovery + polling + JSON parsing
 */

// Try multiple HTTP endpoints to find the device's API
int refossApiType = -1;  // -1=auto-detect, 0=/rpc/Refoss.Status.Get

// ==================== REFOSS DISCOVERY ====================

void discoverRefoss() {
    Serial.print("Probing Refoss EMO6P @ ");
    Serial.print(refossIP);
    Serial.println("...");

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

        int ui = resp.indexOf("\"uuid\":\"");
        if (ui >= 0) {
            int ue = resp.indexOf("\"", ui + 8);
            refossUUID = resp.substring(ui + 8, ue);
        }

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

        // UDP broadcast fallback
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

// ==================== REFOSS POLLING ====================

void pollRefossEnergy() {
    if (!refossFound || refossIP.length() == 0) return;

    HTTPClient http;
    http.setTimeout(5000);
    String resp = "";
    int httpCode = 0;

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
        // Auto-detect
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
                if (resp.length() > 20 && resp.indexOf("{") >= 0
                    && resp.indexOf("invalid") < 0 && resp.indexOf("error") < 0) {
                    refossApiType = i;
                    Serial.print(">>> USING: "); Serial.println(endpoints[i]);
                    break;
                }
                resp = "";
            } else {
                http.end();
                Serial.print("  no-auth HTTP "); Serial.println(httpCode);
            }

            // Try WITH auth
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

    Serial.print("Refoss ("); Serial.print(resp.length()); Serial.print("b): ");
    Serial.println(resp.substring(0, min(500, (int)resp.length())));

    // Parse raw channel data from JSON
    parseRefossResponse(resp);

    // Calculate per-channel VA/VAR + per-phase totals + system totals
    calculateEnergyTotals();

    // Store in aggregation buffer
    storeAggregationSample();

    Serial.print("Energy: Solar="); Serial.print(liveSolar, 0);
    Serial.print("W("); Serial.print(liveSolarVA, 0); Serial.print("VA)");
    Serial.print(" Grid="); Serial.print(liveGrid, 0);
    Serial.print("W("); Serial.print(liveGridVA, 0); Serial.print("VA)");
    Serial.print(" Home="); Serial.print(liveHome, 0);
    Serial.print("W("); Serial.print(liveHomeVA, 0); Serial.print("VA)");
    Serial.print(" Saved="); Serial.print(livePowerSaved, 0);
    Serial.print("W("); Serial.print(livePowerSavedVA, 0); Serial.print("VA)");
    Serial.print(" ("); Serial.print(aggCount); Serial.println(" samples)");

    showWebUI();
}

// ==================== PARSE REFOSS RESPONSE ====================

void parseRefossResponse(String resp) {
    bool found = false;

    // Format 1: Shelly EM — keys like a_act_power, b_act_power, c_act_power
    if (resp.indexOf("act_power") >= 0) {
        liveCh[0].power_w = extractJsonFloat(resp, "\"a_act_power\":");
        liveCh[0].voltage_v = extractJsonFloat(resp, "\"a_voltage\":");
        liveCh[0].current_a = extractJsonFloat(resp, "\"a_current\":");
        liveCh[0].pf = extractJsonFloat(resp, "\"a_pf\":");

        liveCh[1].power_w = extractJsonFloat(resp, "\"b_act_power\":");
        liveCh[1].voltage_v = extractJsonFloat(resp, "\"b_voltage\":");
        liveCh[1].current_a = extractJsonFloat(resp, "\"b_current\":");
        liveCh[1].pf = extractJsonFloat(resp, "\"b_pf\":");

        liveCh[2].power_w = extractJsonFloat(resp, "\"c_act_power\":");
        liveCh[2].voltage_v = extractJsonFloat(resp, "\"c_voltage\":");
        liveCh[2].current_a = extractJsonFloat(resp, "\"c_current\":");
        liveCh[2].pf = extractJsonFloat(resp, "\"c_pf\":");

        liveCh[3].power_w = extractJsonFloat(resp, "\"a2_act_power\":");
        liveCh[4].power_w = extractJsonFloat(resp, "\"b2_act_power\":");
        liveCh[5].power_w = extractJsonFloat(resp, "\"c2_act_power\":");
        liveCh[3].current_a = extractJsonFloat(resp, "\"a2_current\":");
        liveCh[4].current_a = extractJsonFloat(resp, "\"b2_current\":");
        liveCh[5].current_a = extractJsonFloat(resp, "\"c2_current\":");

        found = true;
        Serial.println("Parsed Shelly EM format");
    }
    // Format 2: Refoss RPC — "em:1":{"power":X,"voltage":X,"current":X,...}
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
                    liveCh[idx].energy_wh = extractJsonFloat(obj, "\"day_energy\":") * 1000.0;
                    found = true;
                    Serial.print("  em:"); Serial.print(ch);
                    Serial.print(" P="); Serial.print(liveCh[idx].power_w, 1);
                    Serial.print("W V="); Serial.print(liveCh[idx].voltage_v, 1);
                    Serial.print("V I="); Serial.print(liveCh[idx].current_a, 3);
                    Serial.print("A PF="); Serial.print(liveCh[idx].pf, 2);
                    Serial.println();
                }
            }
        }
        if (found) Serial.println("Parsed Refoss RPC format OK!");
    }

    if (!found) {
        Serial.println("Could not parse energy data");
        return;
    }

    refossDataValid = true;
}

// ==================== JSON HELPERS ====================

float extractJsonFloat(String json, String key) {
    int idx = json.indexOf(key);
    if (idx < 0) return 0;
    int start = idx + key.length();
    int end = json.indexOf(",", start);
    if (end < 0) end = json.indexOf("}", start);
    if (end < 0) return 0;
    return json.substring(start, end).toFloat();
}

String extractJsonValue(String json, String key) {
    int idx = json.indexOf(key);
    if (idx < 0) return "";
    int start = idx + key.length();
    while (start < (int)json.length() && (json[start] == ' ' || json[start] == '\t')) start++;
    int end = json.indexOf(",", start);
    if (end < 0) end = json.indexOf("}", start);
    if (end < 0) return "";
    String val = json.substring(start, end);
    val.trim();
    if (val.startsWith("\"")) val = val.substring(1);
    if (val.endsWith("\"")) val = val.substring(0, val.length() - 1);
    return val;
}

unsigned long getUnixTimestamp() {
    time_t now = time(nullptr);
    if (now > 1000000000) return (unsigned long)now;
    return millis() / 1000;
}
