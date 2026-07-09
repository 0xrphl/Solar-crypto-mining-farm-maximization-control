/*
 * display.ino — TFT Display functions (ST7789)
 */

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
        gfx->print("S:"); gfx->print(liveSolar, 0); gfx->print("W ");
        gfx->print("G:"); gfx->print(liveGrid, 0); gfx->print("W ");
        gfx->print("H:"); gfx->print(liveHome, 0); gfx->print("W");
    } else if (refossFound) {
        gfx->print("EMO6P @ "); gfx->print(refossIP); gfx->print(" ...");
    } else {
        gfx->print("Searching Refoss EMO6P...");
    }

    // Apparent power line
    if (refossDataValid) {
        gfx->setTextColor(0xFD20); // orange
        gfx->setCursor(10, 58);
        gfx->print("VA S:"); gfx->print(liveSolarVA, 0);
        gfx->print(" G:"); gfx->print(liveGridVA, 0);
        gfx->print(" H:"); gfx->print(liveHomeVA, 0);
    }

    // Relay status line
    gfx->setTextColor(WHITE);
    gfx->setCursor(10, 70);
    gfx->print("R1:BN(81W) R2:Oct(180W) S"); gfx->print(miningState);

    // Last action
    if (lastAction.length() > 0) {
        gfx->setTextColor(MAGENTA);
        gfx->setCursor(10, 82);
        gfx->println(lastAction.substring(0, 40));
    }

    // Supabase status
    gfx->setTextColor(0x07E0); // bright green
    gfx->setCursor(10, 96);
    if (lastDecision > 0) {
        unsigned long ago = (millis() - lastDecision) / 1000;
        gfx->print("Supa: "); gfx->print(ago); gfx->print("s ago");
        gfx->print(" ("); gfx->print(aggCount); gfx->print(" smp)");
    } else {
        gfx->print("Supabase: waiting first push");
    }
}
