/*
 * energy.ino — Per-phase energy calculations (scalar, no vector addition)
 *
 * 2-Phase (Bi-Phase) Circuit:
 *
 * Phase 1 (A channels):
 *   A1 (ch1/idx0) = Solar CT      — measured
 *   A2 (ch4/idx3) = Grid CT       — measured
 *   Home Phase 1                  — CALCULATED: Home_P1 = Solar_P1 + Grid_P1
 *
 * Phase 2 (B/C channels — all clamps known):
 *   B1 (ch2/idx1) = House CT      — measured
 *   B2 (ch5/idx4) = Solar CT      — measured
 *   C1 (ch3/idx2) = Shower CT     — measured (house load)
 *   C2 (ch6/idx5) = Grid CT       — measured
 *   Home Phase 2                  — DIRECTLY measured: |B1| + |C1|
 *
 * Per-channel:
 *   apparent_va  = |W| / |PF|   (when |PF| > 0.01, else = |W|)
 *   reactive_var = sqrt(VA² - W²)
 *
 * Per-phase (scalar sums within each phase):
 *   Phase totals computed independently, then summed for system totals.
 *   NO cross-phase vector addition.
 *
 * Power Saved = Solar - Home  (both active W and apparent VA)
 */

// ==================== PER-CHANNEL VA/VAR ====================

// Calculate apparent (VA) and reactive (VAR) for a single channel
void calcChannelApparentReactive(ChannelData &ch) {
    float absPF = fabs(ch.pf);
    float absW  = fabs(ch.power_w);

    if (absPF > 0.01 && absW > 0.1) {
        ch.apparent_va  = absW / absPF;
        float va2 = ch.apparent_va * ch.apparent_va;
        float w2  = absW * absW;
        ch.reactive_var = (va2 > w2) ? sqrt(va2 - w2) : 0;
    } else if (absW > 0.1) {
        // PF near zero but power exists — treat as purely reactive
        ch.apparent_va  = absW;
        ch.reactive_var = absW;
    } else {
        ch.apparent_va  = 0;
        ch.reactive_var = 0;
    }
}

// ==================== ENERGY TOTALS CALCULATION ====================

void calculateEnergyTotals() {
    // Step 1: Calculate VA and VAR for each of the 6 channels
    for (int i = 0; i < 6; i++) {
        calcChannelApparentReactive(liveCh[i]);
    }

    // Step 2: Per-phase calculations (scalar, independent phases)

    // --- PHASE 1 (A channels) ---
    // Solar P1: A1 (idx 0) — always positive
    phase1.solar_w  = fabs(liveCh[0].power_w);
    phase1.solar_va = liveCh[0].apparent_va;

    // Grid P1: A2 (idx 3) — signed (negative = export, positive = import)
    phase1.grid_w  = liveCh[3].power_w;
    phase1.grid_va = liveCh[3].apparent_va;

    // Home P1: CALCULATED from energy balance
    // Home = Solar + Grid  (when grid negative/exporting, home < solar)
    // This gives the active power consumed on Phase 1
    phase1.home_w = phase1.solar_w + phase1.grid_w;
    if (phase1.home_w < 0) phase1.home_w = 0;  // can't consume negative

    // Home P1 apparent: scalar estimate from energy balance
    // When exporting: home_va = solar_va - grid_va (but floor at 0)
    // When importing: home_va = solar_va + grid_va
    if (phase1.grid_w < 0) {
        // Exporting: home uses less than solar produces
        phase1.home_va = phase1.solar_va - phase1.grid_va;
        if (phase1.home_va < 0) phase1.home_va = 0;
    } else {
        // Importing: home uses solar + grid
        phase1.home_va = phase1.solar_va + phase1.grid_va;
    }

    // --- PHASE 2 (B/C channels — all clamps known) ---
    // Solar P2: B2 (idx 4) — always positive
    phase2.solar_w  = fabs(liveCh[4].power_w);
    phase2.solar_va = liveCh[4].apparent_va;

    // Grid P2: C2 (idx 5) — signed
    phase2.grid_w  = liveCh[5].power_w;
    phase2.grid_va = liveCh[5].apparent_va;

    // Home P2: DIRECTLY measured from B1 (idx 1) + C1 (idx 2)
    phase2.home_w  = fabs(liveCh[1].power_w) + fabs(liveCh[2].power_w);
    phase2.home_va = liveCh[1].apparent_va + liveCh[2].apparent_va;

    // Step 3: System totals (scalar sum of phases — NOT vector addition)
    liveSolar   = phase1.solar_w  + phase2.solar_w;
    liveGrid    = phase1.grid_w   + phase2.grid_w;
    liveHome    = phase1.home_w   + phase2.home_w;
    liveSolarVA = phase1.solar_va + phase2.solar_va;
    liveGridVA  = phase1.grid_va  + phase2.grid_va;
    liveHomeVA  = phase1.home_va  + phase2.home_va;

    // Step 4: System reactive power totals
    liveSolarVAR = sqrt(max(0.0f, liveSolarVA * liveSolarVA - liveSolar * liveSolar));
    liveGridVAR  = sqrt(max(0.0f, liveGridVA  * liveGridVA  - liveGrid  * liveGrid));
    liveHomeVAR  = sqrt(max(0.0f, liveHomeVA  * liveHomeVA  - liveHome  * liveHome));

    // Step 5: Power saved — surplus available for mining
    // Active W surplus
    livePowerSaved   = liveSolar - liveHome;
    // Apparent VA surplus
    livePowerSavedVA = liveSolarVA - liveHomeVA;

    // Debug per-phase
    Serial.println("  Phase1: S=" + String(phase1.solar_w, 0) + "W/" + String(phase1.solar_va, 0) + "VA"
                   + " G=" + String(phase1.grid_w, 0) + "W/" + String(phase1.grid_va, 0) + "VA"
                   + " H=" + String(phase1.home_w, 0) + "W/" + String(phase1.home_va, 0) + "VA(calc)");
    Serial.println("  Phase2: S=" + String(phase2.solar_w, 0) + "W/" + String(phase2.solar_va, 0) + "VA"
                   + " G=" + String(phase2.grid_w, 0) + "W/" + String(phase2.grid_va, 0) + "VA"
                   + " H=" + String(phase2.home_w, 0) + "W/" + String(phase2.home_va, 0) + "VA(meas)");
}

// ==================== AGGREGATION BUFFER ====================

void storeAggregationSample() {
    if (aggCount >= MAX_SAMPLES) return;

    int s = aggCount;
    for (int i = 0; i < 6; i++) {
        aggPower[i][s]   = liveCh[i].power_w;
        aggCurrent[i][s] = liveCh[i].current_a;
        aggPf[i][s]      = liveCh[i].pf;
        aggEnergy[i][s]  = liveCh[i].energy_wh;
        aggVA[i][s]      = liveCh[i].apparent_va;
        aggVAR[i][s]     = liveCh[i].reactive_var;
    }
    aggVoltage[0][s] = liveCh[0].voltage_v;  // Phase 1 voltage
    aggVoltage[1][s] = liveCh[1].voltage_v;  // Phase 2 voltage
    aggVoltage[2][s] = liveCh[2].voltage_v;  // Phase 2 (C)

    aggSolar[s]   = liveSolar;
    aggGrid[s]    = liveGrid;
    aggHome[s]    = liveHome;
    aggSolarVA[s] = liveSolarVA;
    aggGridVA[s]  = liveGridVA;
    aggHomeVA[s]  = liveHomeVA;
    aggSaved[s]   = livePowerSaved;
    aggSavedVA[s] = livePowerSavedVA;

    aggCount++;
}
