/*
 * mining.ino — Mining decision engine + state verification
 *
 * Single 9-min cycle: decides optimal profile for ALL miners (relays + Avalon)
 * Surplus = -Grid + currentMiningW (grid-based, accounts for mining consumption)
 * Uses active W for decisions, tracks apparent VA for logging.
 */

// ==================== MINING DECISION ENGINE ====================

void runMiningDecision() {
    if (!autoMiningEnabled || !refossDataValid) return;

    // Recency-weighted averages: recent samples (last 5) count 2x
    float avgSolar = 0, avgHome = 0, avgGrid = 0;
    float avgSolarVA = 0, avgHomeVA = 0, avgSaved = 0, avgSavedVA = 0;
    float totalWeight = 0;
    for (int s = 0; s < aggCount; s++) {
        float weight = (s >= aggCount - 5) ? 2.0 : 1.0;
        avgSolar   += aggSolar[s] * weight;
        avgHome    += aggHome[s] * weight;
        avgGrid    += aggGrid[s] * weight;
        avgSolarVA += aggSolarVA[s] * weight;
        avgHomeVA  += aggHomeVA[s] * weight;
        avgSaved   += aggSaved[s] * weight;
        avgSavedVA += aggSavedVA[s] * weight;
        totalWeight += weight;
    }
    if (totalWeight > 0) {
        avgSolar   /= totalWeight;
        avgHome    /= totalWeight;
        avgGrid    /= totalWeight;
        avgSolarVA /= totalWeight;
        avgHomeVA  /= totalWeight;
        avgSaved   /= totalWeight;
        avgSavedVA /= totalWeight;
    }

    // GRID-BASED SURPLUS: what would be available if all miners were off
    // Grid is already affected by mining consumption (miners have no CTs)
    // So: available = -Grid + currentMining = total surplus independent of current mining
    //
    // RAMP-UP GUARD: After a recent state change, miners may not be at full power yet.
    // Avalon Q takes 2-5 min to ramp up, BitAxe/Octaxe ~30s.
    // During ramp-up, using rated W overestimates → could overshoot profile.
    // Fix: use actual MPO from last verify if available, else rated with 50W margin.
    float currentMiningW;
    if (actualAvalonMPO > 0 && strcmp(profiles[miningState].avalonMode, "off") != 0) {
        // Use real measured power from Avalon (MPO field from last estats query)
        float bitaxeActual = 0;
        for (int i = 0; i < 3; i++) bitaxeActual += actualBitaxePwr[i];
        currentMiningW = actualAvalonMPO + bitaxeActual;
        Serial.print("Mining estimate (measured): Avalon="); Serial.print(actualAvalonMPO);
        Serial.print("W BitAxe="); Serial.print(bitaxeActual, 0);
        Serial.print("W Total="); Serial.print(currentMiningW, 0); Serial.println("W");
    } else {
        // Fallback to rated profile wattage with safety margin
        currentMiningW = profiles[miningState].totalW;
        Serial.print("Mining estimate (rated): "); Serial.print(currentMiningW, 0); Serial.println("W");
    }
    float available = -avgGrid + currentMiningW;
    // Apply 50W margin to prevent oscillation at profile boundaries
    available -= 50;
    float netSurplus = -avgGrid;  // actual export (positive = exporting)

    // Find the highest profile whose totalW fits UNDER available power
    int targetState = -1;
    for (int i = NUM_PROFILES - 1; i >= 0; i--) {
        if (profiles[i].totalW <= available) {
            targetState = i;
            break;
        }
    }
    if (targetState < 0) targetState = 0;

    Serial.println("\n=== MINING DECISION ===");
    Serial.print("Solar: "); Serial.print(avgSolar, 0);
    Serial.print("W("); Serial.print(avgSolarVA, 0); Serial.print("VA)");
    Serial.print("  Home: "); Serial.print(avgHome, 0);
    Serial.print("W("); Serial.print(avgHomeVA, 0); Serial.print("VA)");
    Serial.print("  Available: "); Serial.print(available, 0);
    Serial.print("W  Net: "); Serial.print(netSurplus, 0);
    Serial.print("W  Grid: "); Serial.print(avgGrid, 0); Serial.println("W");
    Serial.print("Power Saved: "); Serial.print(avgSaved, 0);
    Serial.print("W / "); Serial.print(avgSavedVA, 0); Serial.println("VA");
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

    // Control Avalon Q mode
    const char* avNow = profiles[oldState].avalonMode;
    if (strcmp(p.avalonMode, avNow) != 0) {
        if (strcmp(p.avalonMode, "off") == 0) {
            String r = controlAvalon(3, "standby");
            Serial.print("  Avalon Q: sleep → "); Serial.println(r);
        } else {
            if (strcmp(avNow, "off") == 0) {
                String r = controlAvalon(3, "wakeup");
                Serial.print("  Avalon Q: wake → "); Serial.println(r);
                delay(5000);
                lastAvalonCmdTime = 0;
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
                ",\"surplus\":" + String(available, 1) +
                ",\"grid\":" + String(avgGrid, 1) + "}";
    supabaseInsert("transitions", tj);

    Serial.println("========================\n");
    showWebUI();
}

// ==================== PRE-TRANSITION STATE VERIFICATION ====================

void verifyMinerStates() {
    if (!autoMiningEnabled) return;
    
    Serial.println("\n--- VERIFY: Querying actual device states ---");
    
    const MiningProfile &expected = profiles[miningState];
    bool mismatch = false;
    String mismatches = "";
    
    // 1. Check Tasmota relay states
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
    
    // 2. Query BitAxe miners (idx 0,1,2)
    for (int i = 0; i < 3; i++) {
        queryBitaxeActualState(i);
    }
    
    // Cross-check: R1 miners
    bool r1MinersHashing = (actualBitaxeOn[0] || actualBitaxeOn[1]);
    if (expected.relay1 && tasmotaOk && actualR1 && !r1MinersHashing) {
        Serial.println("Verify: R1 ON but miners not hashing yet (may be booting)");
    }
    if (expected.relay2 && tasmotaOk && actualR2 && !actualBitaxeOn[2]) {
        Serial.println("Verify: R2 ON but Octaxe not hashing yet (may be booting)");
    }
    
    // 3. Query Avalon Q
    queryAvalonActualState();
    
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
    
    if (tasmotaOk && actualR1 != expected.relay1) {
        String r1res = controlTasmotaRelay(1, expected.relay1 ? "ON" : "OFF");
        Serial.print("  R1 forced "); Serial.print(expected.relay1 ? "ON" : "OFF");
        Serial.print(": "); Serial.println(r1res);
    }
    if (tasmotaOk && actualR2 != expected.relay2) {
        String r2res = controlTasmotaRelay(2, expected.relay2 ? "ON" : "OFF");
        Serial.print("  R2 forced "); Serial.print(expected.relay2 ? "ON" : "OFF");
        Serial.print(": "); Serial.println(r2res);
    }
    
    // Force Avalon Q state — reset cooldown first so corrections aren't blocked
    lastAvalonCmdTime = 0;  // CRITICAL: allow all commands during correction
    bool avalonExpectedOn = (strcmp(expected.avalonMode, "off") != 0);
    bool avalonActuallyOn = (actualAvalonMode != "off" && actualAvalonMode != "sleep");
    
    if (avalonExpectedOn && !avalonActuallyOn) {
        if (actualAvalonMode == "off") {
            Serial.println("  Avalon Q: unreachable, attempting wake...");
            controlAvalon(3, "wakeup");
            delay(5000);
            lastAvalonCmdTime = 0;
        } else if (actualAvalonMode == "sleep") {
            Serial.println("  Avalon Q: sleeping, waking...");
            controlAvalon(3, "wakeup");
            delay(5000);
            lastAvalonCmdTime = 0;
        }
        // DEFERRED: Don't set workmode now — Avalon needs 30-60s to boot
        // Schedule it for the next poll cycles
        pendingAvalonMode = String(expected.avalonMode);
        pendingAvalonSetTime = millis();
        pendingAvalonRetries = 0;
        Serial.print("  Avalon Q: DEFERRED workmode '"); Serial.print(pendingAvalonMode);
        Serial.println("' — will retry every 30s until Avalon accepts it");
    } else if (!avalonExpectedOn && avalonActuallyOn) {
        Serial.print("  Avalon Q: running in "); Serial.print(actualAvalonMode);
        Serial.println(" but should be OFF, sending standby...");
        controlAvalon(3, "standby");
    } else if (avalonExpectedOn && avalonActuallyOn && actualAvalonMode != String(expected.avalonMode)) {
        Serial.print("  Avalon Q: mode "); Serial.print(actualAvalonMode);
        Serial.print(" → changing to "); Serial.println(expected.avalonMode);
        String r = controlAvalon(3, expected.avalonMode);
        Serial.print("  Result: "); Serial.println(r);
    }
    
    lastAction = "CORRECTED: " + String(expected.name) + mismatches;
    
    // Log correction transition
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

// ==================== ACTUAL DEVICE STATE HELPERS ====================

void queryAvalonActualState() {
    actualAvalonMode = "off";
    actualAvalonMHS = 0;
    actualAvalonMPO = 0;
    
    WiFiClient client;
    client.setTimeout(3000);
    if (!client.connect(miners[3].ip, miners[3].port)) {
        actualAvalonMode = "off";
        Serial.println("Verify Avalon: TCP unreachable → OFF");
        return;
    }
    
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
    if (mhsStr.length() > 0) actualAvalonMHS = mhsStr.toFloat();
    
    Serial.print("Verify Avalon: MHS av="); Serial.print(actualAvalonMHS, 0);
    
    // Send "estats" for WORKMODE and MPO
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
        String softoff = extractBracketValue(resp2, "SoftOFF");
        String state = extractBracketValue(resp2, "STATE");
        String ghsavg = extractBracketValue(resp2, "GHSavg");
        
        if (mpo.length() > 0) actualAvalonMPO = mpo.toInt();
        
        float ghsVal = 0;
        if (ghsavg.length() > 0) ghsVal = ghsavg.toFloat();
        
        Serial.print(" MPO="); Serial.print(actualAvalonMPO);
        Serial.print(" WORKMODE="); Serial.print(wm);
        Serial.print(" SoftOFF="); Serial.print(softoff);
        Serial.print(" STATE="); Serial.print(state);
        Serial.print(" GHSavg="); Serial.print(ghsavg);
        
        int softoffVal = softoff.toInt();
        int stateVal = state.toInt();
        bool activelyHashing = (stateVal == 1 && ghsVal > 100.0);
        
        if (softoffVal > 0 && !activelyHashing) {
            actualAvalonMode = "sleep";
            Serial.print(" → SLEEP (SoftOFF="); Serial.print(softoffVal); Serial.println(", not hashing)");
        } else if (stateVal != 1 && ghsVal < 1.0) {
            actualAvalonMode = "sleep";
            Serial.print(" → SLEEP (STATE="); Serial.print(stateVal); Serial.println(", no hashrate)");
        } else if (actualAvalonMHS < 1000 && ghsVal < 1.0) {
            actualAvalonMode = "sleep";
            Serial.println(" → SLEEP (both MHS av and GHSavg near zero)");
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
            actualAvalonMode = "low";
            Serial.println(" → RUNNING (mode unknown, assume low)");
        }
    } else {
        if (actualAvalonMHS < 1000) {
            actualAvalonMode = "sleep";
            Serial.println(" → SLEEP (estats failed, low hashrate)");
        } else {
            actualAvalonMode = "low";
            Serial.println(" → RUNNING (estats failed, assume low)");
        }
    }
}

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
    
    actualBitaxeOn[idx] = (actualBitaxeHR[idx] > 0 && actualBitaxePwr[idx] > 1.0);
    
    Serial.print("Verify "); Serial.print(miners[idx].name);
    Serial.print(": HR="); Serial.print(actualBitaxeHR[idx], 0);
    Serial.print(" Pwr="); Serial.print(actualBitaxePwr[idx], 1);
    Serial.print("W → "); Serial.println(actualBitaxeOn[idx] ? "ON" : "OFF");
}

bool avalonModeMatches(const char* expected, String actual) {
    if (strcmp(expected, "off") == 0) {
        return (actual == "off" || actual == "sleep");
    }
    return (actual == String(expected));
}
