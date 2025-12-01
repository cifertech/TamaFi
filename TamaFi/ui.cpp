#include <Arduino.h>
#include "ui.h"
#include "ui_anim.h"

// Graphics headers
#include "StoneGolem.h"
#include "egg_hatch.h"
#include "effect.h"
#include "background.h"

static const int TFT_W = 240;
static const int TFT_H = 240;
static const int PET_W = 115;
static const int PET_H = 110;
static const int EFFECT_W = 100;
static const int EFFECT_H = 95;

// Hunting animation
static int huntFrame = 0;
static unsigned long lastHuntFrameTime = 0;
static const int HUNT_FRAME_DELAY = 300;   // adjust speed

// Idle sprite sets per stage (placeholder: same for all)
static const uint16_t* BABY_IDLE_FRAMES[4]  = { idle_1, idle_2, idle_3, idle_4 };
static const uint16_t* TEEN_IDLE_FRAMES[4]  = { idle_1, idle_2, idle_3, idle_4 };
static const uint16_t* ADULT_IDLE_FRAMES[4] = { idle_1, idle_2, idle_3, idle_4 };
static const uint16_t* ELDER_IDLE_FRAMES[4] = { idle_1, idle_2, idle_3, idle_4 };

// Egg frames
static const uint16_t* EGG_FRAMES[5] = {
    egg_hatch_1, egg_hatch_2, egg_hatch_3, egg_hatch_4, egg_hatch_5
};

static const uint16_t* EGG_IDLE_FRAMES[4] = {
    egg_hatch_11, egg_hatch_21, egg_hatch_31, egg_hatch_41
};

static const uint16_t* HUNGER_FRAMES[4] = {
    hunger1, hunger2, hunger3, hunger4
};

static const uint16_t* DEAD_FRAMES[3] = {
    dead_1, dead_2, dead_3
};

// HUNTING animation loop
static const uint16_t* ATTACK_FRAMES[3] = {
    attack_0, attack_1, attack_2
};


// Local UI state
static int idleFrameUi = 0;
static unsigned long lastIdleFrameUi = 0;

static int eggIdleFrameUi = 0;
static unsigned long lastEggIdleTimeUi = 0;

static int hatchFrameUi = 0;
static unsigned long lastHatchFrameUi = 0;

static int deadFrameUi = 0;
static unsigned long lastDeadFrameUi = 0;

// Highlight animation states
static int menuHighlightY        = 30;
static int menuHighlightTargetY  = 30;
static unsigned long lastMenuAnimTime = 0;

static int ctlHighlightY         = 30;
static int ctlHighlightTargetY   = 30;
static unsigned long lastCtlAnim = 0;

static int setHighlightY         = 30;
static int setHighlightTargetY   = 30;
static unsigned long lastSetAnim = 0;

static const int MAIN_MENU_COUNT = 7;

// ---------------------------------------------------------------------------
// UNIVERSAL HIGHLIGHT ALIGNMENT
// ---------------------------------------------------------------------------
static int calcHighlightY(int rowIndex, int rowHeight, int topOffset) {
    return topOffset + rowIndex * rowHeight - 5;  // centered under text
}

static const char* moodTextLocal(Mood m) {
    switch (m) {
        case MOOD_HUNGRY:  return "HUNGRY";
        case MOOD_HAPPY:   return "HAPPY";
        case MOOD_CURIOUS: return "CURIOUS";
        case MOOD_BORED:   return "BORED";
        case MOOD_SICK:    return "SICK";
        case MOOD_EXCITED: return "EXCITED";
        case MOOD_CALM:    return "CALM";
    }
    return "?";
}

static const char* stageTextLocal(Stage s) {
    switch (s) {
        case STAGE_BABY:  return "BABY";
        case STAGE_TEEN:  return "TEEN";
        case STAGE_ADULT: return "ADULT";
        case STAGE_ELDER: return "ELDER";
    }
    return "?";
}

static const char* activityTextLocal(Activity a) {
    switch (a) {
        case ACT_HUNT:     return "HUNTING WIFI...";
        case ACT_DISCOVER: return "DISCOVERING...";
        case ACT_REST:     return "RESTING...";
        default:           return "";
    }
}

static void drawHeader(const char* title) {
    fb.fillRect(0, 0, TFT_W, 18, TFT_BLACK);
    fb.drawLine(0, 18, TFT_W, 18, TFT_CYAN);
    fb.drawLine(0, 19, TFT_W, 19, TFT_MAGENTA);

    fb.fillRect(5, 6, 6, 6, TFT_WHITE);
    fb.fillRect(6, 7, 4, 4, TFT_BLACK);

    fb.setTextColor(TFT_WHITE);
    fb.setCursor(18, 5);
    fb.print(title);
}

static void drawBar(int x, int y, int w, int h, int value, uint16_t color) {
    fb.drawRect(x, y, w, h, TFT_WHITE);
    int fillWidth = (w - 2) * value / 100;
    fb.fillRect(x + 1, y + 1, fillWidth, h - 2, color);
}

static void drawBubble(int x, int y, bool selected) {
    if (selected) {
        fb.fillCircle(x, y, 4, TFT_WHITE);
        fb.fillCircle(x, y, 2, TFT_BLACK);
    } else {
        fb.drawCircle(x, y, 4, TFT_WHITE);
    }
}

static void drawMenuIcon(int iconIndex, int x, int y) {
    switch (iconIndex) {
        case 0: fb.drawRect(x, y+3, 5, 4, TFT_WHITE); fb.fillRect(x+1,y+4,3,2,TFT_WHITE); break;
        case 1: fb.drawLine(x+2,y+8,x+5,y+2,TFT_WHITE); fb.drawLine(x+8,y+8,x+5,y+2,TFT_WHITE); fb.fillRect(x+4,y+8,2,3,TFT_WHITE); break;
        case 2: fb.drawRect(x+1,y+2,8,6,TFT_WHITE); fb.drawPixel(x,y+3,TFT_WHITE); fb.drawPixel(x+9,y+3,TFT_WHITE); break;
        case 3: fb.drawLine(x+1,y+3,x+9,y+3,TFT_WHITE); fb.fillRect(x+3,y+2,3,3,TFT_WHITE); break;
        case 4: fb.drawCircle(x+5,y+5,3,TFT_WHITE); break;
        case 5: fb.fillRect(x+4,y+2,2,2,TFT_WHITE); break;
        case 6: fb.drawLine(x+8,y+4,x+2,y+4,TFT_WHITE); fb.drawLine(x+2,y+4,x+4,y+2,TFT_WHITE); break;
    }
}

static void animateSelector(int &pos, int &target, unsigned long &lastTick) {
    unsigned long now = millis();
    if (now - lastTick < MENU_ANIM_INTERVAL) return;
    lastTick = now;
    if (pos == target) return;

    int diff = target - pos;
    int step = (diff > 0) ? MENU_ANIM_STEP : -MENU_ANIM_STEP;
    if (abs(diff) < MENU_ANIM_STEP) pos = target;
    else pos += step;
}

static const uint16_t** currentIdleSet() {
    switch (petStage) {
        case STAGE_BABY:  return BABY_IDLE_FRAMES;
        case STAGE_TEEN:  return TEEN_IDLE_FRAMES;
        case STAGE_ADULT: return ADULT_IDLE_FRAMES;
        case STAGE_ELDER: return ELDER_IDLE_FRAMES;
    }
    return BABY_IDLE_FRAMES;
}

// ---------------------------------------------------------------------------
// BOOT SCREEN
// ---------------------------------------------------------------------------
static void screenBoot() {
    fb.fillSprite(TFT_BLACK);
    drawHeader("TamaFi v2");

    fb.setTextColor(TFT_WHITE);
    fb.setCursor(20, 60);
    fb.print("WiFi-fed Virtual Pet");

    fb.setCursor(20, 100);
    fb.print("Press any button...");

    fb.pushSprite(0, 0);
}

// ---------------------------------------------------------------------------
// HATCH SCREEN (Idle egg → OK → hatch → home)
// ---------------------------------------------------------------------------
static void screenHatch() {
    ledcWriteTone(5, 0);
    fb.fillSprite(TFT_BLACK);
    drawHeader("Hatching...");

    fb.pushImage(0, 18, TFT_W, TFT_H - 18, backgroundImage2);
    unsigned long now = millis();

    // 1) Idle egg animation until OK pressed
    if (!hasHatchedOnce && !hatchTriggered) {
        if (now - lastEggIdleTimeUi >= EGG_IDLE_DELAY) {
            lastEggIdleTimeUi = now;
            eggIdleFrameUi = (eggIdleFrameUi + 1) % 4;
        }

        petSprite.pushImage(0, 0, PET_W, PET_H, EGG_IDLE_FRAMES[eggIdleFrameUi]);
        petSprite.pushToSprite(&fb, 70, 80, TFT_WHITE);

        //fb.setCursor(10, 200);
        //fb.setTextColor(TFT_WHITE);
        //fb.print("Press OK to hatch");

        fb.pushSprite(0, 0);
        return;
    }

    // 2) Triggered hatch animation
    if (!hasHatchedOnce && hatchTriggered) {
      if (hatchFrameUi == 0) {
        sndHatch();    // <<< PLAY RETRO HATCH SOUND HERE
        }
        if (now - lastHatchFrameUi >= HATCH_DELAY) {
            lastHatchFrameUi = now;

            if (hatchFrameUi < 4) hatchFrameUi++;
            else {
                hasHatchedOnce = true;
                hatchTriggered = false;
                hatchFrameUi   = 0;
                currentScreen  = SCREEN_HOME;
                uiOnScreenChange(currentScreen);
                return;
            }
        }

        petSprite.pushImage(0, 0, PET_W, PET_H, EGG_FRAMES[hatchFrameUi]);
        petSprite.pushToSprite(&fb, 70, 80, TFT_WHITE);

        //fb.setCursor(10, 200);
        //fb.setTextColor(TFT_WHITE);
        //fb.print("Hatching...");

        fb.pushSprite(0, 0);
        return;
    }

    // Safety fallback
    currentScreen = SCREEN_HOME;
    uiOnScreenChange(currentScreen);
}

// ---------------------------------------------------------------------------
// HOME SCREEN
// ---------------------------------------------------------------------------

static void drawStatsBlock() {
    int x = 20, y = 100, w = 80, h = 8;

    drawBar(x, y,       w, h, pet.hunger,    TFT_RED);
    drawBar(x, y + 28,  w, h, pet.happiness, TFT_YELLOW);
    drawBar(x, y + 56,  w, h, pet.health,    TFT_GREEN);

    fb.setTextColor(TFT_BLACK);
    fb.setCursor(x + 3, y + 75);
    fb.print("Mood:  ");
    fb.print(moodTextLocal(currentMood));

    fb.setCursor(x + 3, y + 89);
    fb.print("Stage: ");
    fb.print(stageTextLocal(petStage));
}

static void screenHome() {
    fb.fillSprite(TFT_BLACK);

    // ===== TOP BAR MESSAGE =====
    if (currentActivity != ACT_NONE)
        drawHeader(activityTextLocal(currentActivity));
    else
        drawHeader("Idle");

    fb.pushImage(0, 18, TFT_W, TFT_H - 18, backgroundImage);

    unsigned long now = millis();

    // =============================
    //        REST ANIMATION
    // =============================
    if (currentActivity == ACT_REST && restPhase != REST_NONE) {

        int frameIdx = 0;

        if (restPhase == REST_ENTER) {
            // Going to sleep → 5 → 4 → 3 → 2 → 1
            frameIdx = 4 - constrain(restFrameIndex, 0, 4);
        }
        else if (restPhase == REST_DEEP) {
            // Deep sleeping → always frame 1 (egg_hatch_1)
            frameIdx = 0;
        }
        else if (restPhase == REST_WAKE) {
            // Waking up → 1 → 2 → 3 → 4 → 5
            frameIdx = constrain(restFrameIndex, 0, 4);
        }

        petSprite.pushImage(0, 0, PET_W, PET_H, EGG_FRAMES[frameIdx]);
        petSprite.pushToSprite(&fb, petPosX, petPosY, TFT_WHITE);

        // --- Always draw stats ---
        drawStatsBlock();

        fb.pushSprite(0, 0);
        return;
    }

    // =============================
    //        HUNTING ANIMATION
    // =============================
    if (currentActivity == ACT_HUNT) {

        if (now - lastHuntFrameTime >= HUNT_FRAME_DELAY) {
            lastHuntFrameTime = now;
            huntFrame = (huntFrame + 1) % 3;   // attack_0 → attack_1 → attack_2
        }

        petSprite.pushImage(0, 0, PET_W, PET_H, ATTACK_FRAMES[huntFrame]);
        petSprite.pushToSprite(&fb, petPosX, petPosY, TFT_WHITE);

        drawStatsBlock();

        fb.pushSprite(0, 0);
        return;
    }

    // =============================
    //        IDLE ANIMATION
    // =============================
    int idleSpeed = IDLE_BASE_DELAY;
    if (currentMood == MOOD_EXCITED) idleSpeed = IDLE_FAST_DELAY;
    if (currentMood == MOOD_BORED || currentMood == MOOD_SICK) idleSpeed = IDLE_SLOW_DELAY;

    if (now - lastIdleFrameUi >= (unsigned long)idleSpeed) {
        lastIdleFrameUi = now;
        idleFrameUi = (idleFrameUi + 1) % 4;
    }

    const uint16_t** idleSet = currentIdleSet();
    petSprite.pushImage(0, 0, PET_W, PET_H, idleSet[idleFrameUi]);
    petSprite.pushToSprite(&fb, petPosX, petPosY, TFT_WHITE);

    // =============================
    //         ALWAYS DRAW STATS
    // =============================
    drawStatsBlock();

    // =============================
    //     HUNGER EFFECT OVERLAY
    // =============================
    if (hungerEffectActive) {
        effectSprite.pushImage(0, 0, EFFECT_W, EFFECT_H, HUNGER_FRAMES[hungerEffectFrame]);
        effectSprite.pushToSprite(&fb, 120, 90, TFT_WHITE);
    }

    fb.pushSprite(0, 0);
}


// ---------------------------------------------------------------------------
// MAIN MENU
// ---------------------------------------------------------------------------
static void screenMenu(int mainMenuIndex) {
    fb.fillSprite(TFT_BLACK);
    drawHeader("Main Menu");

    animateSelector(menuHighlightY, menuHighlightTargetY, lastMenuAnimTime);

    fb.fillRect(8, menuHighlightY, 224, 18, TFT_DARKGREY);
    fb.drawRect(8, menuHighlightY, 224, 18, TFT_CYAN);

    const char* items[] = {
        "Pet Status",
        "Environment",
        "System Info",
        "Controls",
        "Settings",
        "Diagnostics",
        "Back"
    };

    int baseY = 30;
    int step  = 20;

    for (int i = 0; i < MAIN_MENU_COUNT; i++) {
        int y = baseY + i * step;

        drawMenuIcon(i, 16, y - 2);

        fb.setCursor(40, y);
        fb.setTextColor(i == mainMenuIndex ? TFT_YELLOW : TFT_WHITE);
        fb.print(items[i]);
    }

    fb.setCursor(10, 200);
    fb.setTextColor(TFT_WHITE);
    fb.print("UP/DOWN = move | OK = select");

    fb.pushSprite(0, 0);
}

// ---------------------------------------------------------------------------
// PET STATUS
// ---------------------------------------------------------------------------
static void screenPetStatus() {
    fb.fillSprite(TFT_BLACK);
    drawHeader("Pet Status");

    fb.setTextColor(TFT_WHITE);

    fb.setCursor(10, 26);
    fb.print("Stage: ");  fb.print(stageTextLocal(petStage));

    fb.setCursor(10,38);
    fb.print("Age:   ");
    fb.print(pet.ageDays); fb.print("d ");
    fb.print(pet.ageHours); fb.print("h ");
    fb.print(pet.ageMinutes); fb.print("m");


    fb.setCursor(10, 56);
    fb.print("Hunger: "); fb.print(pet.hunger); fb.print("%");

    fb.setCursor(10, 68);
    fb.print("Happy:  "); fb.print(pet.happiness); fb.print("%");

    fb.setCursor(10, 80);
    fb.print("Health: "); fb.print(pet.health); fb.print("%");

    fb.setCursor(10, 98);
    fb.print("Mood:   "); fb.print(moodTextLocal(currentMood));

    fb.setCursor(10, 116);
    fb.print("Personality:");

    fb.setCursor(16, 130);
    fb.print("Curiosity: "); fb.print((int)traitCuriosity);

    fb.setCursor(16, 142);
    fb.print("Activity : "); fb.print((int)traitActivity);

    fb.setCursor(16, 154);
    fb.print("Stress   : "); fb.print((int)traitStress);

    fb.setCursor(10, 200);
    fb.print("OK = Back");

    fb.pushSprite(0, 0);
}

// ---------------------------------------------------------------------------
// ENVIRONMENT
// ---------------------------------------------------------------------------
static void screenEnvironment() {
    fb.fillSprite(TFT_BLACK);
    drawHeader("Environment");

    fb.setTextColor(TFT_WHITE);

    fb.setCursor(10, 30);
    fb.print("Networks : "); fb.print(wifiStats.netCount);

    fb.setCursor(10, 42);
    fb.print("Strong   : "); fb.print(wifiStats.strongCount);

    fb.setCursor(10, 54);
    fb.print("Hidden   : "); fb.print(wifiStats.hiddenCount);

    fb.setCursor(10, 66);
    fb.print("Open     : "); fb.print(wifiStats.openCount);

    fb.setCursor(10, 78);
    fb.print("WPA/etc  : "); fb.print(wifiStats.wpaCount);

    fb.setCursor(10, 94);
    fb.print("Avg RSSI : "); fb.print(wifiStats.avgRSSI);

    fb.setCursor(10, 200);
    fb.print("OK = Back");

    fb.pushSprite(0, 0);
}

// ---------------------------------------------------------------------------
// SYSTEM INFO
// ---------------------------------------------------------------------------
static void screenSysInfo() {
    fb.fillSprite(TFT_BLACK);
    drawHeader("System Info");

    fb.setTextColor(TFT_WHITE);

    fb.setCursor(10, 30);
    fb.print("Firmware: 2.0");

    fb.setCursor(10, 42);
    fb.print("MCU:      ESP32");

    fb.setCursor(10, 54);
    fb.print("Heap Free: ");
    fb.print(ESP.getFreeHeap() / 1024); fb.print(" KB");

    unsigned long s = millis() / 1000;
    unsigned long m = s / 60;
    unsigned long h = m / 60;
    s %= 60; m %= 60;

    fb.setCursor(10, 72);
    fb.print("Uptime: ");
    fb.printf("%02lu:%02lu:%02lu", h, m, s);

    fb.setCursor(10, 90);
    fb.print("WiFi Scan: ");
    fb.print(wifiScanInProgress ? "Running" : "Idle");

    fb.setCursor(10, 200);
    fb.print("OK = Back");

    fb.pushSprite(0, 0);
}

// ---------------------------------------------------------------------------
// CONTROLS MENU
// ---------------------------------------------------------------------------
static void screenControls(int controlsIndex) {
    fb.fillSprite(TFT_BLACK);
    drawHeader("Controls");

    animateSelector(ctlHighlightY, ctlHighlightTargetY, lastCtlAnim);

    fb.fillRect(8, ctlHighlightY, 224, 18, TFT_DARKGREY);
    fb.drawRect(8, ctlHighlightY, 224, 18, TFT_CYAN);

    const char* labels[] = {
        "Screen Brightness",
        "LED Brightness",
        "Sound",
        "NeoPixels",
        "Back"
    };

    fb.setTextColor(TFT_WHITE);
    fb.setTextSize(1);

    int baseY = 30;
    int step  = 20;

    for (int i = 0; i < 5; i++) {
        int y = baseY + i * step;

        drawBubble(14, y, i == controlsIndex);

        fb.setCursor(30, y - 4);
        fb.setTextColor(i == controlsIndex ? TFT_YELLOW : TFT_WHITE);
        fb.print(labels[i]);

        fb.setCursor(150, y - 4);
        fb.setTextColor(TFT_CYAN);

        switch (i) {
            case 0:
                fb.print(tftBrightnessIndex==0?"Low":tftBrightnessIndex==1?"Mid":"High");
                break;
            case 1:
                fb.print(ledBrightnessIndex==0?"Low":ledBrightnessIndex==1?"Mid":"High");
                break;
            case 2:
                fb.print(soundEnabled?"On":"Off");
                break;
            case 3:
                fb.print(neoPixelsEnabled?"On":"Off");
                break;
        }
    }

    fb.setCursor(10, 200);
    fb.print("OK = Select/Back");

    fb.pushSprite(0, 0);
}

// ---------------------------------------------------------------------------
// SETTINGS MENU
// ---------------------------------------------------------------------------
static void screenSettings(int settingsMenuIndex) {
    fb.fillSprite(TFT_BLACK);
    drawHeader("Settings");

    animateSelector(setHighlightY, setHighlightTargetY, lastSetAnim);

    fb.fillRect(8, setHighlightY, 224, 18, TFT_DARKGREY);
    fb.drawRect(8, setHighlightY, 224, 18, TFT_CYAN);

    const char* labels[] = {
        "Theme",
        "Auto Sleep",
        "Auto Save",
        "Reset Pet",
        "Reset All",
        "Back"
    };

    int baseY = 30;
    int step  = 18;

    for (int i = 0; i < 6; i++) {
        int y = baseY + i * step;

        drawBubble(14, y, i == settingsMenuIndex);

        fb.setCursor(30, y - 4);
        fb.setTextColor(i == settingsMenuIndex ? TFT_YELLOW : TFT_WHITE);
        fb.print(labels[i]);

        fb.setCursor(150, y - 4);
        fb.setTextColor(TFT_CYAN);

        switch (i) {
            case 0: fb.print("Pixel"); break;
            case 1: fb.print(autoSleep?"On":"Off"); break;
            case 2: fb.print(autoSaveMs/1000); fb.print("s"); break;
        }
    }

    fb.setCursor(10, 200);
    fb.print("OK = Select");

    fb.pushSprite(0, 0);
}

// ---------------------------------------------------------------------------
// DIAGNOSTICS
// ---------------------------------------------------------------------------
static void screenDiagnostics() {
    fb.fillSprite(TFT_BLACK);
    drawHeader("Diagnostics");

    fb.setTextColor(TFT_WHITE);

    fb.setCursor(10, 30);
    fb.print("Activity: ");
    fb.print(activityTextLocal(currentActivity));

    fb.setCursor(10, 42);
    fb.print("Mood: ");
    fb.print(moodTextLocal(currentMood));

    fb.setCursor(10, 54);
    fb.print("RestPhase: ");
    fb.print(restPhase==REST_ENTER?"ENTER":
             restPhase==REST_DEEP ?"DEEP":
             restPhase==REST_WAKE ?"WAKE":"NONE");

    fb.setCursor(10, 66);
    fb.print("WiFi Scan: ");
    fb.print(wifiScanInProgress?"Running":"Idle");

    fb.setCursor(10, 200);
    fb.print("OK = Back");

    fb.pushSprite(0, 0);
}

// ---------------------------------------------------------------------------
// GAME OVER
// ---------------------------------------------------------------------------
static void screenGameOver() {
    fb.fillSprite(TFT_BLACK);
    drawHeader("Game Over");

    unsigned long now = millis();
    if (now - lastDeadFrameUi >= DEAD_DELAY) {
        lastDeadFrameUi = now;
        //deadFrameUi = (deadFrameUi + 1) % DEAD_FRAME_COUNT;
        deadFrameUi++;
        if (deadFrameUi > 2) deadFrameUi = 2;
    }

    fb.pushImage(0, 18, TFT_W, TFT_H - 18, backgroundImage);

    petSprite.pushImage(0, 0, PET_W, PET_H, DEAD_FRAMES[deadFrameUi]);
    petSprite.pushToSprite(&fb, petPosX, petPosY, TFT_WHITE);

    //fb.setCursor(10, 200);
    //fb.setTextColor(TFT_WHITE);
    //fb.print("OK = Restart");

    fb.pushSprite(0, 0);
}

// ---------------------------------------------------------------------------
// PUBLIC UI API
// ---------------------------------------------------------------------------
void uiInit() {
    idleFrameUi = 0;
    lastIdleFrameUi = millis();

    eggIdleFrameUi = 0;
    lastEggIdleTimeUi = millis();

    hatchFrameUi = 0;
    lastHatchFrameUi = millis();

    deadFrameUi = 0;
    lastDeadFrameUi = millis();
}

void uiOnScreenChange(Screen newScreen) {
    if (newScreen == SCREEN_MENU) {
        menuHighlightY = menuHighlightTargetY = calcHighlightY(mainMenuIndex, 20, 30);
    }
    if (newScreen == SCREEN_CONTROLS) {
        ctlHighlightY  = ctlHighlightTargetY = calcHighlightY(controlsIndex, 20, 30);
    }
    if (newScreen == SCREEN_SETTINGS) {
        setHighlightY  = setHighlightTargetY = calcHighlightY(settingsMenuIndex, 18, 30);
    }
    if (newScreen == SCREEN_HATCH) {
        eggIdleFrameUi = hatchFrameUi = 0;
    }
}

void uiDrawScreen(Screen screen,
                  int mainMenuIdx,
                  int controlsIdx,
                  int settingsIdx)
{
    if (screen == SCREEN_MENU) {
        menuHighlightTargetY = calcHighlightY(mainMenuIdx, 20, 30);
    }
    if (screen == SCREEN_CONTROLS) {
        ctlHighlightTargetY = calcHighlightY(controlsIdx, 20, 30) - 4;
    }
    if (screen == SCREEN_SETTINGS) {
        setHighlightTargetY = calcHighlightY(settingsMenuIndex, 18, 30) - 4;
    }

    switch (screen) {
        case SCREEN_BOOT:        screenBoot(); break;
        case SCREEN_HATCH:       screenHatch(); break;
        case SCREEN_HOME:        screenHome(); break;
        case SCREEN_MENU:        screenMenu(mainMenuIdx); break;
        case SCREEN_PET_STATUS:  screenPetStatus(); break;
        case SCREEN_ENVIRONMENT: screenEnvironment(); break;
        case SCREEN_SYSINFO:     screenSysInfo(); break;
        case SCREEN_CONTROLS:    screenControls(controlsIdx); break;
        case SCREEN_SETTINGS:    screenSettings(settingsIdx); break;
        case SCREEN_DIAGNOSTICS: screenDiagnostics(); break;
        case SCREEN_GAMEOVER:    screenGameOver(); break;
    }
}
