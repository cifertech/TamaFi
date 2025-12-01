#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <math.h>

#include "ui.h"
#include "ui_anim.h"

// Graphics
#include "StoneGolem.h"
#include "egg_hatch.h"
#include "effect.h"
#include "background.h"

// --- Hatching state ---
bool hasHatchedOnce = false;
bool hatchTriggered = false;

// --------- Hardware pins ---------
#define BTN_UP    13
#define BTN_OK    12
#define BTN_DOWN  11

#define BTN_RIGHT1 8
#define BTN_RIGHT2 9
#define BTN_RIGHT3 10

#define LED_PIN    1
#define LED_COUNT  4

#define BUZZER_PIN    2
#define BUZZER_CH     5

#define TFT_BRIGHTNESS_PIN 7

// TFT sizes
#define TFT_W 240
#define TFT_H 240
#define PET_W 115
#define PET_H 110
#define EFFECT_W 100
#define EFFECT_H 95

// --------- Global objects ---------
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite fb(&tft);
TFT_eSprite petSprite(&tft);
TFT_eSprite effectSprite(&tft);

Adafruit_NeoPixel leds(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
Preferences prefs;

// --------- Shared game state (matching ui.h externs) ---------
Screen    currentScreen = SCREEN_BOOT;
Activity  currentActivity = ACT_NONE;
RestPhase restPhase = REST_NONE;

Pet       pet;
WifiStats wifiStats;

Mood      currentMood = MOOD_CALM;
Stage     petStage    = STAGE_BABY;

bool      hungerEffectActive = false;
int       hungerEffectFrame  = 0;

bool      wifiScanInProgress = false;
unsigned long lastWifiScanTime = 0;
unsigned long lastSaveTime     = 0;

bool      soundEnabled     = true;
bool      neoPixelsEnabled = true;
uint8_t   tftBrightnessIndex = 1;
uint8_t   ledBrightnessIndex = 1;
bool      autoSleep          = true;
uint16_t  autoSaveMs         = 30000;

uint8_t   traitCuriosity = 70;
uint8_t   traitActivity  = 60;
uint8_t   traitStress    = 40;

unsigned long lastDecisionTime      = 0;
uint32_t      currentDecisionInterval = 10000;

int       restFrameIndex = 0;

// --------- Internal logic timers ---------
unsigned long hungerTimer    = 0;
unsigned long happinessTimer = 0;
unsigned long healthTimer    = 0;
unsigned long ageTimer       = 0;
unsigned long lastLogicTick  = 0;

// Rest
unsigned long lastRestAnimTime = 0;
unsigned long restPhaseStart   = 0;
unsigned long restDurationMs   = 0;
bool          restStatsApplied = false;

// Hunger overlay
unsigned long lastHungerFrameTime = 0;

// Death
unsigned long lastDeadFrameTime = 0;

// Buttons (edge)
bool lastUp   = HIGH;
bool lastOk   = HIGH;
bool lastDown = HIGH;

bool lastR1 = HIGH;
bool lastR2 = HIGH;
bool lastR3 = HIGH;

// Menus
int mainMenuIndex     = 0;
int controlsIndex     = 0;
int settingsMenuIndex = 0;

// Buzzer
unsigned long buzzerEndTime = 0;

// Wifi decision randomness
const uint32_t DECISION_INTERVAL_MIN = 8000;
const uint32_t DECISION_INTERVAL_MAX = 15000;

// ------- Forward declarations -------
void sndClick();
void sndGoodFeed();
void sndBadFeed();
void sndDiscover();
void sndRestStart();
void sndRestEnd();

// --------- Basic helpers ---------
bool buttonPressed(int pin, bool &lastState) {
  bool s = digitalRead(pin);
  bool p = (lastState == HIGH && s == LOW);
  lastState = s;
  return p;
}

// -------------------- NeoPixel Core --------------------
void ledsOff() {
  if (!neoPixelsEnabled) return;     // If disabled, leave off state
  for (int i = 0; i < LED_COUNT; i++)
    leds.setPixelColor(i, 0);
  leds.show();                       // ALWAYS show after writing pixels
}

// Brightness must only change .setBrightness()
// Never rewrite pixels unless needed
void applyLedBrightness() {
  uint8_t bri =
      (ledBrightnessIndex == 0) ? 20 :
      (ledBrightnessIndex == 1) ? 90 :
                                  180;

  leds.setBrightness(bri);

  if (!neoPixelsEnabled) {
    ledsOff();
    return;
  }

  leds.show();     // update brightness instantly
}

// -------------------- Color Helpers --------------------
static inline uint32_t C(uint8_t r, uint8_t g, uint8_t b) {
  return leds.Color(r, g, b);
}

// -------------------- Visual Effects --------------------
void ledsHappy() {
  if (!neoPixelsEnabled) return;
  uint32_t col = C(120, 40, 200);    // soft violet
  for (int i = 0; i < LED_COUNT; i++) leds.setPixelColor(i, col);
  leds.show();
}

void ledsSad() {
  if (!neoPixelsEnabled) return;
  uint32_t col = C(200, 0, 0);       // dim red (non-blinding)
  for (int i = 0; i < LED_COUNT; i++) leds.setPixelColor(i, col);
  leds.show();
}

void ledsWifi() {
  if (!neoPixelsEnabled) return;
  uint32_t col = C(0, 90, 255);      // softer blue
  for (int i = 0; i < LED_COUNT; i++) leds.setPixelColor(i, col);
  leds.show();
}

void ledsRest() {
  if (!neoPixelsEnabled) return;
  uint32_t col = C(0, 25, 90);       // calm deep-blue
  for (int i = 0; i < LED_COUNT; i++) leds.setPixelColor(i, col);
  leds.show();
}


// ---------- Buzzer core ----------
void stopBuzzerIfNeeded() {
  if (buzzerEndTime == 0) return;
  if (millis() > buzzerEndTime) {
    ledcWriteTone(BUZZER_CH, 0);
    buzzerEndTime = 0;
  }
}

void buzzerPlay(int freq, int durMs) {
  if (!soundEnabled) return;
  ledcWriteTone(BUZZER_CH, freq);
  buzzerEndTime = millis() + durMs;
}

// ---------- Ultra-Retro sequencer ----------

struct RetroSound {
  const int *freqs;
  const int *times;
  int length;
};

int sndIndex = -1;
int sndStep  = 0;
unsigned long sndNext = 0;

const int CLICK_FREQS[] = { 2100, 1600, 900 };
const int CLICK_TIMES[] = {  20,   20,   20 };
RetroSound SND_CLICK = { CLICK_FREQS, CLICK_TIMES, 3 };

const int GOOD_FREQS[] = { 600, 900, 1200, 1500 };
const int GOOD_TIMES[] = { 40,  40,  40,   60   };
RetroSound SND_GOOD = { GOOD_FREQS, GOOD_TIMES, 4 };

const int BAD_FREQS[] = { 900, 700, 500, 300 };
const int BAD_TIMES[] = { 50,  50,  60,  80   };
RetroSound SND_BAD = { BAD_FREQS, BAD_TIMES, 4 };

const int DISC_FREQS[] = { 400, 650, 900, 1200, 1500 };
const int DISC_TIMES[] = { 40,  40,  40,  40,   60    };
RetroSound SND_DISC = { DISC_FREQS, DISC_TIMES, 5 };

const int REST_START_FREQS[] = { 600, 400, 300 };
const int REST_START_TIMES[] = { 60,  70,  90  };
RetroSound SND_REST_START = { REST_START_FREQS, REST_START_TIMES, 3 };

const int REST_END_FREQS[] = { 300, 500, 700 };
const int REST_END_TIMES[] = { 60,  60,  80  };
RetroSound SND_REST_END = { REST_END_FREQS, REST_END_TIMES, 3 };

const int HATCH_FREQS[] = { 500, 800, 1200, 1600, 2000 };
const int HATCH_TIMES[] = {  60,  60,   60,   80,  100 };
RetroSound SND_HATCH = { HATCH_FREQS, HATCH_TIMES, 5 };

void sndUpdate() {
  if (!soundEnabled) {
    ledcWriteTone(BUZZER_CH, 0);
    sndIndex = -1;
    sndStep = 0;
    return;
  }

  if (sndIndex < 0) return;

  unsigned long now = millis();
  if (now >= sndNext) {
    const RetroSound *snd = nullptr;

    switch (sndIndex) {
      case 0: snd = &SND_CLICK;       break;
      case 1: snd = &SND_GOOD;        break;
      case 2: snd = &SND_BAD;         break;
      case 3: snd = &SND_DISC;        break;
      case 4: snd = &SND_REST_START;  break;
      case 5: snd = &SND_REST_END;    break;
      case 6: snd = &SND_HATCH;       break;
      default: sndIndex = -1; sndStep = 0; return;
    }

    if (sndStep >= snd->length) {
      ledcWriteTone(BUZZER_CH, 0);
      sndIndex = -1;
      sndStep = 0;
      return;
    }

    ledcWriteTone(BUZZER_CH, snd->freqs[sndStep]);
    sndNext = now + snd->times[sndStep];
    sndStep++;
  }
}

// ---- public sound API ----
void sndClick()       { if (!soundEnabled) return; sndIndex = 0; sndStep = 0; }
void sndGoodFeed()    { if (!soundEnabled) return; sndIndex = 1; sndStep = 0; ledsHappy(); }
void sndBadFeed()     { if (!soundEnabled) return; sndIndex = 2; sndStep = 0; ledsSad(); }
void sndDiscover()    { if (!soundEnabled) return; sndIndex = 3; sndStep = 0; ledsWifi(); }
void sndRestStart()   { if (!soundEnabled) return; sndIndex = 4; sndStep = 0; }
void sndRestEnd()     { if (!soundEnabled) return; sndIndex = 5; sndStep = 0; }
void sndHatch()       { if (!soundEnabled) return; sndIndex = 6; sndStep = 0; }

// ---------- TFT brightness ----------
void applyTftBrightness() {
  uint8_t val = (tftBrightnessIndex == 0) ? 60 :
                (tftBrightnessIndex == 1) ? 150 : 255;
  ledcWrite(0, val);
}

// ---------- WiFi scan ----------
void startWifiScan() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  WiFi.scanNetworks(true);
  wifiScanInProgress = true;
}

bool checkWifiScanDone() {
  if (!wifiScanInProgress) return false;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return false;

  wifiScanInProgress = false;
  lastWifiScanTime = millis();

  if (n < 0) {
    wifiStats = WifiStats();
    WiFi.scanDelete();
    return true;
  }

  WifiStats s;
  s.netCount    = n;
  s.strongCount = 0;
  s.hiddenCount = 0;
  s.openCount   = 0;
  s.wpaCount    = 0;
  int totalRSSI = 0;

  for (int i = 0; i < n; i++) {
    int rssi = WiFi.RSSI(i);
    totalRSSI += rssi;
    if (rssi > -60) s.strongCount++;
    if (WiFi.SSID(i).length() == 0) s.hiddenCount++;
    if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) s.openCount++;
    else s.wpaCount++;
  }

  s.avgRSSI = (n > 0) ? (totalRSSI / n) : -100;
  wifiStats = s;

  WiFi.scanDelete();
  return true;
}

// ---------- Persistence ----------
void saveState() {
  prefs.putInt("hunger", pet.hunger);
  prefs.putInt("happy",  pet.happiness);
  prefs.putInt("health", pet.health);
  
  prefs.putULong("ageMin", pet.ageMinutes);
  prefs.putULong("ageHr",  pet.ageHours);
  prefs.putULong("ageDay", pet.ageDays);

  prefs.putUChar("stage",  (uint8_t)petStage);
  prefs.putBool("hatched", hasHatchedOnce);

  prefs.putBool("sound", soundEnabled);
  prefs.putUChar("tftBri", tftBrightnessIndex);
  prefs.putUChar("ledBri", ledBrightnessIndex);
  prefs.putBool("neo", neoPixelsEnabled);

  prefs.putUChar("tCur", traitCuriosity);
  prefs.putUChar("tAct", traitActivity);
  prefs.putUChar("tStr", traitStress);
}

void loadState() {
  int h = prefs.getInt("hunger", -1);
  if (h == -1) {
    pet.hunger     = 70;
    pet.happiness  = 70;
    pet.health     = 70;
    pet.ageMinutes = 0;

    petStage       = STAGE_BABY;
    hasHatchedOnce = false;

    soundEnabled       = true;
    tftBrightnessIndex = 1;
    ledBrightnessIndex = 1;
    neoPixelsEnabled   = true;

    traitCuriosity = random(40, 90);
    traitActivity  = random(30, 90);
    traitStress    = random(20, 80);

    saveState();
    return;
  }

  pet.hunger     = prefs.getInt("hunger", 70);
  pet.happiness  = prefs.getInt("happy",  70);
  pet.health     = prefs.getInt("health", 70);
  
  pet.ageMinutes = prefs.getULong("ageMin", 0);
  pet.ageHours   = prefs.getULong("ageHr", 0);
  pet.ageDays    = prefs.getULong("ageDay", 0);


  petStage       = (Stage)prefs.getUChar("stage", (uint8_t)STAGE_BABY);
  hasHatchedOnce = prefs.getBool("hatched", false);

  soundEnabled       = prefs.getBool("sound", true);
  tftBrightnessIndex = prefs.getUChar("tftBri", 1);
  ledBrightnessIndex = prefs.getUChar("ledBri", 1);
  neoPixelsEnabled   = prefs.getBool("neo", true);

  traitCuriosity = prefs.getUChar("tCur", 70);
  traitActivity  = prefs.getUChar("tAct", 60);
  traitStress    = prefs.getUChar("tStr", 40);
}

// ---------- Mood & evolution ----------
void updateMood() {
  if (pet.health < 25 || (wifiStats.netCount == 0 && lastWifiScanTime > 0 &&
                          millis() - lastWifiScanTime > 60000)) {
    currentMood = MOOD_SICK;
    return;
  }

  if (pet.hunger < 25) {
    currentMood = MOOD_HUNGRY;
    return;
  }

  if (pet.happiness > 80 && wifiStats.netCount > 8) {
    currentMood = MOOD_EXCITED;
    return;
  }

  if (pet.happiness > 60 && wifiStats.netCount > 0) {
    currentMood = MOOD_HAPPY;
    return;
  }

  if (wifiStats.netCount == 0 && millis() - lastWifiScanTime > 30000) {
    currentMood = MOOD_BORED;
    return;
  }

  if (wifiStats.hiddenCount > 0 || wifiStats.openCount > 0) {
    currentMood = MOOD_CURIOUS;
    return;
  }

  currentMood = MOOD_CALM;
}

void updateEvolution() {
  unsigned long a = pet.ageMinutes;
  int avg = (pet.hunger + pet.happiness + pet.health) / 3;

  if (a >= 180 && avg > 40 && petStage < STAGE_ELDER) {
    petStage = STAGE_ELDER;
    sndDiscover();
  } else if (a >= 60 && avg > 45 && petStage < STAGE_ADULT) {
    petStage = STAGE_ADULT;
    sndDiscover();
  } else if (a >= 20 && avg > 35 && petStage < STAGE_TEEN) {
    petStage = STAGE_TEEN;
    sndDiscover();
  }
}

// ---------- Rest state machine ----------
void stepRest() {
  if (currentActivity != ACT_REST || restPhase == REST_NONE) return;

  unsigned long now = millis();

  switch (restPhase) {
    case REST_ENTER:
      // Going to sleep: egg_hatch_5 -> 4 -> 3 -> 2 -> 1
      if (now - lastRestAnimTime >= REST_ENTER_DELAY) {
        lastRestAnimTime = now;

        if (restFrameIndex > 0) {
          restFrameIndex--;          // 4,3,2,1,0
        } else {
          // reached egg_hatch_1 (index 0) → deep sleep
          restFrameIndex   = 0;
          restPhase        = REST_DEEP;
          restPhaseStart   = now;
          restStatsApplied = false;
        }
      }
      break;

    case REST_DEEP:
      // Stay on egg_hatch_1 while sleeping
      // (restFrameIndex is kept at 0)
      if (neoPixelsEnabled) {
        float phase = (now - restPhaseStart) / (float)REST_BREATHE_MS;
        int breathe = (int)(sin(phase) * 40.0f + 60.0f);
        breathe = constrain(breathe, 0, 255);
        for (int i = 0; i < LED_COUNT; i++)
          leds.setPixelColor(i, leds.Color(0, 0, breathe));
        leds.show();
      }

      if (!restStatsApplied && now - restPhaseStart > restDurationMs / 2) {
        int hungerDelta    = -3;
        int happinessDelta = +10;
        int healthDelta    = +15;

        pet.hunger    = constrain(pet.hunger + hungerDelta, 0, 100);
        pet.happiness = constrain(pet.happiness + happinessDelta, 0, 100);
        pet.health    = constrain(pet.health + healthDelta, 0, 100);
        restStatsApplied = true;
      }

      if (now - restPhaseStart >= restDurationMs) {
        // time to wake up
        restPhase        = REST_WAKE;
        restPhaseStart   = now;
        lastRestAnimTime = now;
        sndRestEnd();
        ledsOff();
        restFrameIndex = 0;           // start from egg_hatch_1
      }
      break;

    case REST_WAKE:
      // Waking: egg_hatch_1 -> 2 -> 3 -> 4 -> 5
      if (now - lastRestAnimTime >= REST_WAKE_DELAY) {
        lastRestAnimTime = now;

        if (restFrameIndex < 4) {
          restFrameIndex++;           // 0,1,2,3,4
        } else {
          // done, back to idle
          restFrameIndex   = 4;
          restPhase        = REST_NONE;
          currentActivity  = ACT_NONE;
        }
      }
      break;

    default:
      break;
  }
}



// ---------- WiFi-based activities ----------
void resolveHunt() {
  int n = wifiStats.netCount;
  int hungerDelta = 0;
  int happyDelta  = 0;
  int healthDelta = 0;

  if (n == 0) {
    hungerDelta = -15;
    happyDelta  = -10;
    healthDelta = -5;
    sndBadFeed();
  } else {
    hungerDelta = min(35, n * 2 + wifiStats.strongCount * 3);
    int varietyScore = wifiStats.hiddenCount * 2 + wifiStats.openCount;
    happyDelta = min(30, varietyScore * 3 + (wifiStats.avgRSSI + 100) / 3);

    if (wifiStats.avgRSSI > -75) healthDelta += 5;
    if (wifiStats.avgRSSI > -65) healthDelta += 5;
    if (wifiStats.strongCount > 5) healthDelta += 3;

    sndGoodFeed();
  }

  pet.hunger    = constrain(pet.hunger + hungerDelta, 0, 100);
  pet.happiness = constrain(pet.happiness + happyDelta, 0, 100);
  pet.health    = constrain(pet.health + healthDelta, 0, 100);

  hungerEffectActive    = true;
  hungerEffectFrame     = 0;
  lastHungerFrameTime   = millis();
}

void resolveDiscover() {
  int n = wifiStats.netCount;
  int happyDelta  = 0;
  int hungerDelta = 0;

  if (n == 0) {
    happyDelta  = -5;
    hungerDelta = -3;
    sndBadFeed();
  } else {
    int curiosity = wifiStats.hiddenCount * 4 + wifiStats.openCount * 3;
    curiosity += wifiStats.netCount;
    happyDelta  = min(35, curiosity / 2);
    hungerDelta = -5;
    sndDiscover();
  }

  pet.happiness = constrain(pet.happiness + happyDelta, 0, 100);
  pet.hunger    = constrain(pet.hunger + hungerDelta, 0, 100);
}

// ---------- Autonomous decisions ----------
void decideNextActivity() {
  if (currentActivity != ACT_NONE || restPhase != REST_NONE) return;

  unsigned long now = millis();
  if (now - lastDecisionTime < currentDecisionInterval) return;

  lastDecisionTime = now;
  currentDecisionInterval = random(DECISION_INTERVAL_MIN, DECISION_INTERVAL_MAX);

  int desireHunt = 0;
  int desireDisc = 0;
  int desireRest = 0;
  int desireIdle = 10;

  desireHunt = (100 - pet.hunger) + traitCuriosity / 2;
  if (wifiStats.netCount == 0) desireHunt /= 2;

  desireDisc = traitCuriosity + wifiStats.hiddenCount * 10 + wifiStats.openCount * 6 +
               wifiStats.netCount * 2 + random(0, 20);
  if (wifiStats.netCount == 0) desireDisc /= 2;

  desireRest = (100 - pet.health) + traitStress / 2;
  if (pet.hunger < 20) desireRest -= 10;

  if (currentMood == MOOD_HUNGRY) {
    desireHunt += 20;
    desireRest -= 10;
  }
  if (currentMood == MOOD_CURIOUS) {
    desireDisc += 15;
  }
  if (currentMood == MOOD_SICK) {
    desireRest += 20;
    desireDisc -= 10;
  }
  if (currentMood == MOOD_EXCITED) {
    desireDisc += 10;
    desireHunt += 5;
  }
  if (currentMood == MOOD_BORED) {
    desireDisc += 10;
    desireHunt += 5;
  }

  desireHunt = max(desireHunt, 0);
  desireDisc = max(desireDisc, 0);
  desireRest = max(desireRest, 0);
  desireIdle = max(desireIdle, 0);

  int best = desireIdle;
  Activity chosen = ACT_NONE;

  if (desireHunt > best) { best = desireHunt; chosen = ACT_HUNT; }
  if (desireDisc > best) { best = desireDisc; chosen = ACT_DISCOVER; }
  if (desireRest > best) { best = desireRest; chosen = ACT_REST; }

  if (chosen == ACT_NONE) return;

  if (chosen == ACT_HUNT || chosen == ACT_DISCOVER) {
    currentActivity = chosen;
    ledsWifi();
    startWifiScan();
  } else if (chosen == ACT_REST) {
  currentActivity  = ACT_REST;
  restPhase        = REST_ENTER;
  restFrameIndex   = 4;                        // start from egg_hatch_5
  lastRestAnimTime = millis();
  restPhaseStart   = millis();
  restDurationMs   = random(REST_MIN_DURATION, REST_MAX_DURATION);
  restStatsApplied = false;
  sndRestStart();
  ledsRest();
  }
}

// ---------- Reset pet ----------
void resetPet(bool fullReset) {
  pet.hunger    = 70;
  pet.happiness = 70;
  pet.health    = 70;
  if (fullReset) pet.ageMinutes = 0;

  wifiStats = WifiStats();
  lastWifiScanTime = 0;

  unsigned long now = millis();
  hungerTimer    = now;
  happinessTimer = now;
  healthTimer    = now;
  ageTimer       = now;

  currentActivity    = ACT_NONE;
  restPhase          = REST_NONE;
  hungerEffectActive = false;
  wifiScanInProgress = false;
  ledsOff();
}

// ---------- Logic tick ----------
void logicTick() {
  unsigned long now = millis();

  if (now - hungerTimer >= 5000) {
    pet.hunger = max(0, pet.hunger - 2);
    hungerTimer = now;
  }
  if (now - happinessTimer >= 7000) {
    if (wifiStats.netCount == 0 && (now - lastWifiScanTime) > 30000) {
      pet.happiness = max(0, pet.happiness - 3);
    } else {
      pet.happiness = max(0, pet.happiness - 1);
    }
    happinessTimer = now;
  }
  if (now - healthTimer >= 10000) {
    if (pet.hunger < 20 || pet.happiness < 20) {
      pet.health = max(0, pet.health - 2);
    } else {
      pet.health = max(0, pet.health - 1);
    }
    healthTimer = now;
  }

  if (now - ageTimer >= 60000) {
    pet.ageMinutes++;
  
    if (pet.ageMinutes >= 60) {
      pet.ageMinutes -= 60;
      pet.ageHours++;
    }
  
    if (pet.ageHours >= 24) {
      pet.ageHours -= 24;
      pet.ageDays++;
    }
  
    ageTimer = now;
  }


  // Hunger effect
  if (hungerEffectActive && now - lastHungerFrameTime >= HUNGER_EFFECT_DELAY) {
    lastHungerFrameTime = now;
    hungerEffectFrame++;
    if (hungerEffectFrame >= HUNGER_FRAME_COUNT) {
      hungerEffectActive = false;
      ledsOff();
    }
  }

  // WiFi-based activity
  if (currentActivity == ACT_HUNT || currentActivity == ACT_DISCOVER) {
    if (checkWifiScanDone()) {
      if (currentActivity == ACT_HUNT)      resolveHunt();
      else if (currentActivity == ACT_DISCOVER) resolveDiscover();
      currentActivity = ACT_NONE;
      ledsOff();
    }
  }

  // Rest phases
  stepRest();

  // Mood & evolution
  updateMood();
  updateEvolution();

  // Death
  if (pet.hunger <= 0 && pet.happiness <= 0 && pet.health <= 0 &&
      currentScreen != SCREEN_GAMEOVER) {
    currentScreen  = SCREEN_GAMEOVER;
    uiOnScreenChange(currentScreen);
    currentActivity = ACT_NONE;
    restPhase = REST_NONE;
    ledsSad();
  }

  // Autosave
  if (now - lastSaveTime >= autoSaveMs) {
    lastSaveTime = now;
    saveState();
  }

  // Autonomous in HOME
  if (currentScreen == SCREEN_HOME &&
      currentActivity == ACT_NONE &&
      restPhase == REST_NONE) {
    decideNextActivity();
  }

//  stopBuzzerIfNeeded();
}

// ---------- Button handling ----------
void handleButtons() {
  bool up   = buttonPressed(BTN_UP,   lastUp);
  bool ok   = buttonPressed(BTN_OK,   lastOk);
  bool down = buttonPressed(BTN_DOWN, lastDown);

  bool r1 = buttonPressed(BTN_RIGHT1, lastR1);
  bool r2 = buttonPressed(BTN_RIGHT2, lastR2);
  bool r3 = buttonPressed(BTN_RIGHT3, lastR3);
  
  // ===== DIRECT QUICK-ACCESS PAGES =====
  if (currentScreen == SCREEN_HOME) {
      if (r1) {
          sndClick();
          currentScreen = SCREEN_PET_STATUS;
          uiOnScreenChange(currentScreen);
          return;
      }
      if (r2) {
          sndClick();
          currentScreen = SCREEN_ENVIRONMENT;
          uiOnScreenChange(currentScreen);
          return;
      }
      if (r3) {
          sndClick();
          currentScreen = SCREEN_DIAGNOSTICS;
          uiOnScreenChange(currentScreen);
          return;
      }
  }
  
  // ===== RETURN TO HOME WHEN ALREADY IN QUICK PAGE =====
  if (currentScreen == SCREEN_PET_STATUS ||
      currentScreen == SCREEN_ENVIRONMENT ||
      currentScreen == SCREEN_DIAGNOSTICS) {
  
      if (r1 || r2 || r3) {
          sndClick();
          currentScreen = SCREEN_HOME;
          uiOnScreenChange(currentScreen);
          return;
      }
  }

  // BOOT
  if (currentScreen == SCREEN_BOOT) {
    if (up || ok || down) {
      sndClick();
      currentScreen = hasHatchedOnce ? SCREEN_HOME : SCREEN_HATCH;
      uiOnScreenChange(currentScreen);
    }
    return;
  }

  // HATCH
    if (currentScreen == SCREEN_HATCH) {
      if (ok && !hasHatchedOnce) {
        sndClick();
        hatchTriggered = true;   // UI will pick this up and run egg_hatch_1..5
      }
      return;
    }

  // HOME + Hotkeys
  if (currentScreen == SCREEN_HOME) {
  
      if (buttonPressed(BTN_RIGHT1, lastR1)) {
          sndClick();
          currentScreen = SCREEN_PET_STATUS;
          uiOnScreenChange(currentScreen);
          return;
      }
  
      if (buttonPressed(BTN_RIGHT2, lastR2)) {
          sndClick();
          currentScreen = SCREEN_ENVIRONMENT;
          uiOnScreenChange(currentScreen);
          return;
      }
  
      if (buttonPressed(BTN_RIGHT3, lastR3)) {
          sndClick();
          currentScreen = SCREEN_DIAGNOSTICS;
          uiOnScreenChange(currentScreen);
          return;
      }
  
      if (ok) {
          sndClick();
          currentScreen = SCREEN_MENU;
          mainMenuIndex = 0;
          uiOnScreenChange(currentScreen);
      }
  
      return;
  }


  // MAIN MENU
  if (currentScreen == SCREEN_MENU) {
    if (up) {
      sndClick();
      mainMenuIndex = (mainMenuIndex - 1 + 7) % 7;
    }
    if (down) {
      sndClick();
      mainMenuIndex = (mainMenuIndex + 1) % 7;
    }
    if (ok) {
      sndClick();
      switch (mainMenuIndex) {
        case 0: currentScreen = SCREEN_PET_STATUS;   break;
        case 1: currentScreen = SCREEN_ENVIRONMENT;  break;
        case 2: currentScreen = SCREEN_SYSINFO;      break;
        case 3: currentScreen = SCREEN_CONTROLS;     break;
        case 4: currentScreen = SCREEN_SETTINGS;     break;
        case 5: currentScreen = SCREEN_DIAGNOSTICS;  break;
        case 6: currentScreen = SCREEN_HOME;         break;
      }
      uiOnScreenChange(currentScreen);
    }
    return;
  }

  // Simple OK-back pages
  if (currentScreen == SCREEN_PET_STATUS ||
      currentScreen == SCREEN_ENVIRONMENT ||
      currentScreen == SCREEN_SYSINFO ||
      currentScreen == SCREEN_DIAGNOSTICS) {
    if (ok) {
      sndClick();
      currentScreen = SCREEN_MENU;
      uiOnScreenChange(currentScreen);
    }
    return;
  }

  // CONTROLS
  if (currentScreen == SCREEN_CONTROLS) {
    if (up) {
      sndClick();
      controlsIndex = (controlsIndex - 1 + 5) % 5;
    }
    if (down) {
      sndClick();
      controlsIndex = (controlsIndex + 1) % 5;
    }
    if (ok) {
      sndClick();
      switch (controlsIndex) {
        case 0:
          tftBrightnessIndex = (tftBrightnessIndex + 1) % 3;
          applyTftBrightness();
          break;
        case 1:
          ledBrightnessIndex = (ledBrightnessIndex + 1) % 3;
          applyLedBrightness();
          break;
        case 2:
          soundEnabled = !soundEnabled;
          if (!soundEnabled) {
            ledcWriteTone(BUZZER_CH, 0);
            buzzerEndTime = 0;
          }
          break;
        case 3:
          neoPixelsEnabled = !neoPixelsEnabled;
          if (!neoPixelsEnabled) ledsOff();
          else applyLedBrightness();
          break;
        case 4:
          currentScreen = SCREEN_MENU;
          uiOnScreenChange(currentScreen);
          break;
      }
    }
    return;
  }

  // SETTINGS
  if (currentScreen == SCREEN_SETTINGS) {
    if (up) {
      sndClick();
      settingsMenuIndex = (settingsMenuIndex - 1 + 6) % 6;
    }
    if (down) {
      sndClick();
      settingsMenuIndex = (settingsMenuIndex + 1) % 6;
    }
    if (ok) {
      sndClick();
      switch (settingsMenuIndex) {
        case 0:
          // Theme (only Pixel for now)
          break;
        case 1:
          autoSleep = !autoSleep;
          break;
        case 2:
          if (autoSaveMs == 15000) autoSaveMs = 30000;
          else if (autoSaveMs == 30000) autoSaveMs = 60000;
          else autoSaveMs = 15000;
          break;
        case 3:
          resetPet(false);
          break;
        case 4:
          resetPet(true);
          petStage = STAGE_BABY;
          hasHatchedOnce = false;
          saveState();
          currentScreen = SCREEN_HATCH;
          uiOnScreenChange(currentScreen);
          return;
        case 5:
          currentScreen = SCREEN_MENU;
          uiOnScreenChange(currentScreen);
          break;
      }
    }
    return;
  }

  // GAME OVER
  if (currentScreen == SCREEN_GAMEOVER) {
    if (ok) {
      sndClick();
      resetPet(true);
      petStage = STAGE_BABY;
      hasHatchedOnce = false;
      saveState();
      currentScreen = SCREEN_HATCH;
      uiOnScreenChange(currentScreen);
    }
    return;
  }
}

void startupBreathing(uint8_t r, uint8_t g, uint8_t b) {
  static uint16_t t = 0;

  // breathing curve: 0 → 255 → 0
  float brightness = (sin(t / 25.0) + 1.0) * 0.5 * 255;

  for (int i = 0; i < LED_COUNT; i++) {
    leds.setPixelColor(i, leds.Color(
      (r * brightness) / 255,
      (g * brightness) / 255,
      (b * brightness) / 255
    ));
  }

  leds.show();
  t++;
  vTaskDelay(1);
}

// ---------- HACK: declare hatchSequenceStarted from ui.cpp ----------
bool hatchSequenceStarted = false;


// ---------- setup & loop ----------
void setup() {

  Serial.end();
  delay(50);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  leds.begin();
  leds.setBrightness(40);
  leds.clear();
  leds.show();
  
  randomSeed(esp_random());

  pinMode(BTN_UP,   INPUT_PULLUP);
  pinMode(BTN_OK,   INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_RIGHT1, INPUT_PULLUP);
  pinMode(BTN_RIGHT2, INPUT_PULLUP);
  pinMode(BTN_RIGHT3, INPUT_PULLUP);

  tft.init();
  tft.setRotation(0);
  tft.setSwapBytes(true);

  fb.setColorDepth(16);
  fb.createSprite(TFT_W, TFT_H);
  fb.setSwapBytes(true);

  petSprite.setColorDepth(16);
  petSprite.createSprite(PET_W, PET_H);

  effectSprite.setColorDepth(16);
  effectSprite.createSprite(EFFECT_W, EFFECT_H);

  // TFT backlight PWM
  ledcSetup(0, 12000, 8);
  ledcAttachPin(TFT_BRIGHTNESS_PIN, 0);

  // Buzzer PWM
  ledcSetup(BUZZER_CH, 4000, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_CH);
  ledcWriteTone(BUZZER_CH, 0);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);

  prefs.begin("tamafi2", false);
  loadState();
  applyTftBrightness();
  applyLedBrightness();

  unsigned long now = millis();
  hungerTimer      = now;
  happinessTimer   = now;
  healthTimer      = now;
  ageTimer         = now;
  lastLogicTick    = now;
  lastSaveTime     = now;
  lastDecisionTime = now;
  lastRestAnimTime = now;
  lastHungerFrameTime = now;
  lastDeadFrameTime   = now;

  currentScreen = SCREEN_BOOT;
  uiInit();
  uiOnScreenChange(currentScreen);

  startupBreathing(0, 150, 255);
}

void loop() {
  unsigned long now = millis();

  sndUpdate();         
  
  if (!neoPixelsEnabled) {
    ledsOff();   
  } else {
      for (int i = 0; i < LED_COUNT; i++) leds.setPixelColor(i, 0);
  }

  stopBuzzerIfNeeded(); 

  if (now - lastLogicTick >= 100) {
    lastLogicTick = now;
    if (currentScreen != SCREEN_BOOT && currentScreen != SCREEN_HATCH) {
      logicTick();
    }
  }

  handleButtons();
  uiDrawScreen(currentScreen, mainMenuIndex, controlsIndex, settingsMenuIndex);
}
