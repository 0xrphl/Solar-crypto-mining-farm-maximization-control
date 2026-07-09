/*
 * supabase.ino — Supabase REST API push
 * Pushes per-channel W/PF, system totals, per-phase breakdown,
 * apparent VA, power saved, and current mining profile.
 */

// ==================== PUSH TO SUPABASE ====================

void pushToSupabase() {
    if (aggCount == 0) return;

    Serial.print("Pushing "); Serial.print(aggCount);
    Serial.println(" samples to Supabase...");

    // Calculate averages for all fields
    float avgPwr[6] = {0}, avgPf[6] = {0}, avgVA_ch[6] = {0}, avgVAR_ch[6] = {0};
    float avgSol = 0, avgGrd = 0, avgHm = 0;
    float avgSolVA = 0, avgGrdVA = 0, avgHmVA = 0;
    float avgSv = 0, avgSvVA = 0;

    for (int s = 0; s < aggCount; s++) {
        for (int i = 0; i < 6; i++) {
            avgPwr[i]    += aggPower[i][s];
            avgPf[i]     += aggPf[i][s];
            avgVA_ch[i]  += aggVA[i][s];
            avgVAR_ch[i] += aggVAR[i][s];
        }
        avgSol   += aggSolar[s];
        avgGrd   += aggGrid[s];
        avgHm    += aggHome[s];
        avgSolVA += aggSolarVA[s];
        avgGrdVA += aggGridVA[s];
        avgHmVA  += aggHomeVA[s];
        avgSv    += aggSaved[s];
        avgSvVA  += aggSavedVA[s];
    }
    for (int i = 0; i < 6; i++) {
        avgPwr[i]    /= aggCount;
        avgPf[i]     /= aggCount;
        avgVA_ch[i]  /= aggCount;
        avgVAR_ch[i] /= aggCount;
    }
    avgSol   /= aggCount;
    avgGrd   /= aggCount;
    avgHm    /= aggCount;
    avgSolVA /= aggCount;
    avgGrdVA /= aggCount;
    avgHmVA  /= aggCount;
    avgSv    /= aggCount;
    avgSvVA  /= aggCount;

    // Compute per-phase averages from per-channel averages
    float solarP1 = fabs(avgPwr[0]);              // |A1| Solar Phase 1
    float solarP2 = fabs(avgPwr[4]);              // |B2| Solar Phase 2
    float gridP1  = avgPwr[3];                     // A2 Grid Phase 1 (signed)
    float gridP2  = avgPwr[5];                     // C2 Grid Phase 2 (signed)
    float homeP1  = max(0.0f, solarP1 + gridP1);  // Calculated
    float homeP2  = fabs(avgPwr[1]) + fabs(avgPwr[2]); // |B1| + |C1| Measured

    // JSON for "energy" table — all columns (v4 schema)
    String json = "{";
    // Per-channel W + PF (original v3 columns)
    json += "\"a1_w\":" + String(avgPwr[0], 1) + ",\"a1_pf\":" + String(avgPf[0], 2) + ",";
    json += "\"a2_w\":" + String(avgPwr[3], 1) + ",\"a2_pf\":" + String(avgPf[3], 2) + ",";
    json += "\"b1_w\":" + String(avgPwr[1], 1) + ",\"b1_pf\":" + String(avgPf[1], 2) + ",";
    json += "\"b2_w\":" + String(avgPwr[4], 1) + ",\"b2_pf\":" + String(avgPf[4], 2) + ",";
    json += "\"c1_w\":" + String(avgPwr[2], 1) + ",\"c1_pf\":" + String(avgPf[2], 2) + ",";
    json += "\"c2_w\":" + String(avgPwr[5], 1) + ",\"c2_pf\":" + String(avgPf[5], 2) + ",";
    // System totals (original v3 columns)
    json += "\"solar\":" + String(avgSol, 1) + ",";
    json += "\"grid\":" + String(avgGrd, 1) + ",";
    json += "\"home\":" + String(avgHm, 1) + ",";
    // NEW v4: Per-phase breakdown
    json += "\"solar_p1\":" + String(solarP1, 1) + ",";
    json += "\"solar_p2\":" + String(solarP2, 1) + ",";
    json += "\"grid_p1\":" + String(gridP1, 1) + ",";
    json += "\"grid_p2\":" + String(gridP2, 1) + ",";
    json += "\"home_p1\":" + String(homeP1, 1) + ",";
    json += "\"home_p2\":" + String(homeP2, 1) + ",";
    // NEW v4: Apparent power totals
    json += "\"solar_va\":" + String(avgSolVA, 1) + ",";
    json += "\"grid_va\":" + String(avgGrdVA, 1) + ",";
    json += "\"home_va\":" + String(avgHmVA, 1) + ",";
    // NEW v4: Power saved (surplus)
    json += "\"saved_w\":" + String(avgSv, 1) + ",";
    json += "\"saved_va\":" + String(avgSvVA, 1) + ",";
    // NEW v4: Current mining profile
    json += "\"profile_id\":" + String(miningState) + ",";
    // Sample count
    json += "\"n\":" + String(aggCount);
    json += "}";

    bool ok = supabaseInsert("energy", json);
    Serial.println(ok ? "Supabase: OK" : "Supabase: FAILED");

    aggCount = 0;
    showWebUI();
}

// ==================== SUPABASE INSERT ====================

bool supabaseInsert(String table, String jsonBody) {
    WiFiClientSecure client;
    client.setInsecure();

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
