/*
  Tamafi - pocket pet on ESP32-S3 and ST7789 240x240
  CiferTech | MIT License | v1.0.0

  Arduino IDE
  -----------
  1. Boards Manager: install "esp32" by Espressif Systems
  2. Tools -> Board -> ESP32S3 Dev Module
     USB Mode: Hardware CDC and JTAG
     USB CDC On Boot: Enabled
     PSRAM: Disabled
  3. Sketch -> Include Library -> Manage Libraries, install:
     - "TFT_eSPI" (Bodmer)
     - "Adafruit NeoPixel"
  4. Open this folder (Tamafi.ino) and click Upload

  Pins are in tamafi_pins.h. config.h loads the TFT_eSPI setup.
  One 240x240 16-bit framebuffer only. No working PSRAM.

  Buttons (GPIO 6 UP / 15 OK / 18 DOWN / 39 RIGHT1 / 45 RIGHT2 / 48 RIGHT3):
    Face: UP/OK/DOWN open status menus.
    RIGHT1 pet, RIGHT2 environment, RIGHT3 log (press again to close).
    In a menu they navigate. During hatch any press skips.
    Tilt looks. Shake dizzy. Voice listen.
    USB 115200: Lab sends PING, device replies PONG TAMAFI.
*/

#include "config.h"
#include <TFT_eSPI.h>
#include "face.h"
#include "mpu.h"
#include "mic.h"
#include "wifi_pet.h"
#include "ble_pet.h"
#include "pet_state.h"
#include "pet_feedback.h"
#include "pet_dialogue.h"
#include "status_screen.h"
#include "event_log.h"
#include "stamps.h"
#include "host_link.h"
#include "tamafi_usb.h"
#include "hatch.h"

#include <ctype.h>
#include <string.h>

static TFT_eSPI tft;
static uint32_t lastPress = 0;
static uint32_t imuMoodUntil = 0;
static uint32_t petMoodUntil = 0;
static TamafiMood moodBeforeImu = TAMAFI_MOOD_IDLE;
static TamafiMood moodBeforeMic = TAMAFI_MOOD_IDLE;
static TamafiMood moodBeforePet = TAMAFI_MOOD_IDLE;
static TamafiMood moodBeforePreview = TAMAFI_MOOD_IDLE;
static bool imuOwnsMood = false;
static bool micOwnsMood = false;
static bool petOwnsMood = false;
static bool previewOwnsMood = false;
static bool statusVisible = false;
static bool wasHearing = false;
static uint32_t micListenAt = 0;

static const int kButtons[] = {
    PIN_BTN_UP, PIN_BTN_OK, PIN_BTN_DOWN, PIN_BTN_RIGHT1, PIN_BTN_RIGHT2, PIN_BTN_RIGHT3};
static const int kButtonCount = sizeof(kButtons) / sizeof(kButtons[0]);

#if TAMAFI_DEBUG_HEAP
static void logHeap(const char* label) {
  Serial.printf("heap %-12s free=%u min=%u psram=%u\n", label, ESP.getFreeHeap(),
                ESP.getMinFreeHeap(), ESP.getFreePsram());
}
#else
static void logHeap(const char*) {}
#endif

static bool canSteerLook() {
  TamafiMood m = faceMood();
  return !faceBusy() && m != TAMAFI_MOOD_SLEEP && m != TAMAFI_MOOD_BLINK &&
         m != TAMAFI_MOOD_WINK && m != TAMAFI_MOOD_DIZZY &&
         m != TAMAFI_MOOD_AIR_ATTACK && m != TAMAFI_MOOD_AIR_NOISE;
}

static void imuTakeMood(TamafiMood mood, uint32_t holdMs) {
  if (faceBusy()) return;
  if (!imuOwnsMood) moodBeforeImu = faceMood();
  imuOwnsMood = true;
  micOwnsMood = false;
  petOwnsMood = false;
  previewOwnsMood = false;
  imuMoodUntil = millis() + holdMs;
  faceSetMood(mood);
  Serial.printf("imu: %s\n", faceMoodName(mood));
}

static int s_logWifiN = -1;
static int s_logBleN = -1;
static bool s_micLogged = false;
static uint8_t s_btnHeld = 0;

static const char* buttonName(int pin) {
  switch (pin) {
    case PIN_BTN_UP: return "UP";
    case PIN_BTN_OK: return "OK";
    case PIN_BTN_DOWN: return "DOWN";
    case PIN_BTN_RIGHT1: return "RIGHT1";
    case PIN_BTN_RIGHT2: return "RIGHT2";
    case PIN_BTN_RIGHT3: return "RIGHT3";
    default: return "?";
  }
}

static void armButtons() {
  for (int i = 0; i < kButtonCount; ++i) pinMode(kButtons[i], INPUT_PULLUP);
}

static void dumpButtons(int pin) {
  Serial.printf("btn %s GPIO%d  UP=%d OK=%d DN=%d R1=%d R2=%d R3=%d\n", buttonName(pin), pin,
                digitalRead(PIN_BTN_UP), digitalRead(PIN_BTN_OK), digitalRead(PIN_BTN_DOWN),
                digitalRead(PIN_BTN_RIGHT1), digitalRead(PIN_BTN_RIGHT2),
                digitalRead(PIN_BTN_RIGHT3));
}

static StatusPage rightShortcutPage(int pin) {
  if (pin == PIN_BTN_RIGHT1) return STATUS_PET;
  if (pin == PIN_BTN_RIGHT2) return STATUS_ENV;
  if (pin == PIN_BTN_RIGHT3) return STATUS_LOG;
  return STATUS_MENU;
}

static void stampsFromLabel(const char* label) {
  if (!label) return;
  if (strstr(label, "hunt-ok")) stampsNote(STAMP_HUNTER);
  else if (strstr(label, "woke") || strstr(label, "rest-start")) stampsNote(STAMP_NAP);
  else if (strstr(label, "evolve")) stampsNote(STAMP_GROWN);
  else if (strstr(label, "air-attack")) stampsNote(STAMP_HOSTILE);
  else if (strstr(label, "air-noisy")) stampsNote(STAMP_STATIC);
  else if (strstr(label, "air-open")) stampsNote(STAMP_OPEN);
  else if (strstr(label, "heard")) stampsNote(STAMP_HEARD);
}

static bool skipLogLabel(const char* label) {
  if (!label || !label[0]) return true;
  if (strstr(label, "look")) return true;
  static const char* skip[] = {"fidget", "idle",     "wonder", "content", "shy",
                               "huh",    "peckish", "lonely", "play",    "air-busy"};
  for (uint8_t i = 0; i < sizeof(skip) / sizeof(skip[0]); ++i) {
    if (!strcmp(label, skip[i])) return true;
  }
  return false;
}

static LogKind kindFromLabel(const char* label) {
  if (strstr(label, "hunt")) return LOG_HUNT;
  if (strstr(label, "discover")) return LOG_DISC;
  if (strstr(label, "rest") || strstr(label, "woke") || strstr(label, "tired")) return LOG_REST;
  if (strstr(label, "air")) return LOG_AIR;
  if (strstr(label, "wifi") || strstr(label, "quiet-wifi")) return LOG_WIFI;
  if (strstr(label, "ble")) return LOG_BLE;
  return LOG_PET;
}

static void logPetAction(const char* label, const char* line) {
  if (skipLogLabel(label)) return;
  eventLogPush(kindFromLabel(label), (line && line[0]) ? line : label);
}

static void logWifiWindow(const WifiPetStats& stats) {
  if (stats.threat == AIR_HOSTILE) {
    eventLogPushf(LOG_AIR, "deauth x%u", stats.deauth);
  } else if (stats.threat == AIR_NOISY) {
    eventLogPushf(LOG_AIR, "probes %u", stats.probe);
  } else if (stats.threat == AIR_OPEN && stats.openCount > 0) {
    eventLogPushf(LOG_AIR, "%d open ap", stats.openCount);
  }
  if (stats.netCount != s_logWifiN) {
    s_logWifiN = stats.netCount;
    if (stats.avgRSSI > -100) {
      eventLogPushf(LOG_WIFI, "%d ap %ddbm", stats.netCount, stats.avgRSSI);
    } else {
      eventLogPushf(LOG_WIFI, "%d ap", stats.netCount);
    }
  }
}

static void logBleWindow(const BlePetStats& stats) {
  if (stats.deviceCount == s_logBleN) return;
  s_logBleN = stats.deviceCount;
  eventLogPushf(LOG_BLE, "%d nodes", stats.deviceCount);
}

static void drainLogWhileMenu() {
  PetAction action;
  if (petStateConsumeAction(action)) {
    TamafiMood mood = action.mood;
    const char* line = nullptr;
    petDialoguePick(action.label, mood, line);
    logPetAction(action.label, line);
    stampsFromLabel(action.label);
  }
  const TamafiGesture g = mpuConsumeGesture();
  if (g == TAMAFI_GESTURE_SHAKE) petStateOnShaken();
  else if (g == TAMAFI_GESTURE_NUDGE) petStateOnNudged();
  else if (g == TAMAFI_GESTURE_TILT_HOLD) petStateOnTiltHeld();
  if (g != TAMAFI_GESTURE_NONE) eventLogPush(LOG_IMU, mpuGestureName(g));
  const bool hearing = micOk() && micHearing();
  if (hearing && !s_micLogged) eventLogPush(LOG_MIC, "listen");
  s_micLogged = hearing;
}

static void petTakeAction(const PetAction& action) {
  const bool air = action.mood == TAMAFI_MOOD_AIR_ATTACK || action.mood == TAMAFI_MOOD_AIR_NOISE ||
                   action.mood == TAMAFI_MOOD_AIR_OPEN;
  if (!air && faceBusy()) {
    logPetAction(action.label, nullptr);
    stampsFromLabel(action.label);
    return;
  }
  if (!petOwnsMood) moodBeforePet = faceMood();
  petOwnsMood = true;
  petMoodUntil = millis() + action.holdMs;
  TamafiMood mood = action.mood;
  const char* line = nullptr;
  petDialoguePick(action.label, mood, line);
  if (action.mood == TAMAFI_MOOD_SLEEP || mood == TAMAFI_MOOD_SLEEP) faceClearLook();
  faceSetMood(mood);
  if (line) {
    uint16_t sayMs = action.holdMs;
    if (sayMs > 3400) sayMs = 3400;
    if (sayMs < 1600) sayMs = 1600;
    faceSay(line, sayMs);
  }
  petFeedbackEvent(action.label, mood);
  logPetAction(action.label, line);
  stampsFromLabel(action.label);
  Serial.printf("pet: %s -> %s", action.label, faceMoodName(mood));
  if (line) Serial.printf(" \"%s\"", line);
  Serial.println();
}

static PetSignalStats wifiToPetStats(const WifiPetStats& stats) {
  return {stats.netCount,       stats.strongCount, stats.hiddenCount, stats.openCount,
          stats.avgRSSI,        stats.deauth,      stats.probe,       (uint8_t)stats.threat};
}

static PetSignalStats bleToPetStats(const BlePetStats& stats) {
  return {stats.deviceCount, stats.strongCount, 0, 0, stats.avgRSSI, 0, 0, 0};
}

static int consumeButtonPress() {
  const int injected = hostLinkTakeKey();
  if (injected >= 0) {
    lastPress = millis();
    return injected;
  }
  int found = -1;
  for (int i = 0; i < kButtonCount; ++i) {
    const bool down = digitalRead(kButtons[i]) == LOW;
    const uint8_t bit = (uint8_t)(1 << i);
    if (down && !(s_btnHeld & bit) && found < 0 && millis() - lastPress > 220) {
      found = kButtons[i];
      lastPress = millis();
    }
    if (down) s_btnHeld = (uint8_t)(s_btnHeld | bit);
    else s_btnHeld = (uint8_t)(s_btnHeld & ~bit);
  }
  return found;
}

void hostLinkOnPreview(char c) {
  char name[2] = {c, 0};
  hostLinkOnPreviewName(name);
}

bool hostLinkOnPreviewName(const char* name) {
  const TamafiMood mood = faceMoodFromName(name);
  if (mood >= TAMAFI_MOOD_COUNT) return false;

  uint16_t holdMs = 1200;
  if (mood == TAMAFI_MOOD_WINK) holdMs = 900;
  else if (mood == TAMAFI_MOOD_BLINK) holdMs = 500;
  else if (mood == TAMAFI_MOOD_LAUGH) holdMs = 1800;
  else if (mood == TAMAFI_MOOD_AIR_ATTACK) holdMs = 1800;
  else if (mood == TAMAFI_MOOD_AIR_NOISE) holdMs = 1600;
  else if (mood == TAMAFI_MOOD_AIR_OPEN) holdMs = 1200;
  else if (mood == TAMAFI_MOOD_DIZZY) holdMs = 1600;
  else if (mood == TAMAFI_MOOD_SLEEP) holdMs = 1200;
  else if (mood == TAMAFI_MOOD_IDLE) holdMs = 700;

  if (!previewOwnsMood) moodBeforePreview = faceMood();
  imuOwnsMood = false;
  micOwnsMood = false;
  petOwnsMood = true;
  previewOwnsMood = true;
  petMoodUntil = millis() + holdMs;
  if (mood == TAMAFI_MOOD_SLEEP) faceClearLook();
  faceSetMood(mood);
  petFeedbackEvent("preview", mood);
  Serial.printf("preview: %s\n", faceMoodName(mood));
  return true;
}

void setup() {
  tamafiHostBegin();
  delay(100);
  logHeap("boot");
  pinMode(PIN_CAM_PWDN, OUTPUT);
  digitalWrite(PIN_CAM_PWDN, HIGH);
  pinMode(PIN_CAM_RESET, OUTPUT);
  digitalWrite(PIN_CAM_RESET, LOW);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
  for (int i = 0; i < kButtonCount; ++i) pinMode(kButtons[i], INPUT_PULLUP);

  mpuInit();
  logHeap("mpu");
  micInit();
  logHeap("mic");
  petFeedbackInit();
  logHeap("feedback");

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  logHeap("tft");

  faceInit(tft);
  logHeap("face");
  statusMenuInit(tft);
  wifiPetInit();
  logHeap("wifi");
  petStateInit();
  logHeap("pet");
  hatchStart();
  statusMenuSyncPet();
  stampsInit();
  stampsNote(STAMP_ONLINE);
  blePetInit();
  logHeap("ble");
  armButtons();
  eventLogPush(LOG_SYS, "face online");
  Serial.println("Tamafi: EMO ready");
  Serial.println("btn test: press each key, Serial should show that pin = 0");
}

void loop() {
  if (hatchBusy()) {
    hatchTick();
    petFeedbackUpdate();
    hostLinkTick();
    if (consumeButtonPress() >= 0) hatchSkip();
    return;
  }

  if (!statusVisible && !statusMenuAsleep()) {
    const bool overlay = stampsWantsOverlay();
    if (faceTick(!overlay)) {
      if (overlay) {
        stampsTick(true);
        facePresentSprite();
      }
    }
  }
  statusMenuPowerTick();
  petFeedbackUpdate();
  mpuUpdate();
  micUpdate();
  wifiPetUpdate(!blePetScanning());
  blePetUpdate(!wifiPetBusy());

  WifiPetStats wifiStats;
  if (wifiPetConsumeStats(wifiStats)) {
    petStateApplyWifi(wifiToPetStats(wifiStats));
    logWifiWindow(wifiStats);
    if (wifiStats.netCount >= 12) stampsNote(STAMP_CROWD);
  }

  BlePetStats bleStats;
  if (blePetConsumeStats(bleStats)) {
    petStateApplyBle(bleToPetStats(bleStats));
    logBleWindow(bleStats);
  }
  petStateUpdate();
  {
    const uint32_t age = petStateSnapshot().ageMinutes;
    if (age >= 1440) stampsNote(STAMP_DAY);
    if (age >= 10080) stampsNote(STAMP_WEEK);
  }

  hostLinkTick();

  const int pressedButton = consumeButtonPress();
  if (pressedButton >= 0) {
    dumpButtons(pressedButton);
    if (stampsDismiss()) return;
    if (statusMenuWakeIfAsleep()) return;
    imuOwnsMood = false;
    micOwnsMood = false;
    petOwnsMood = false;
    previewOwnsMood = false;
    faceClearLook();

    const StatusPage shortcut = rightShortcutPage(pressedButton);
    if (shortcut != STATUS_MENU) {
      if (statusVisible && statusMenuPage() == shortcut) {
        statusVisible = false;
        tft.fillScreen(TFT_BLACK);
        Serial.printf("btn %s: face\n", buttonName(pressedButton));
      } else {
        statusVisible = true;
        statusMenuOpenQuick(shortcut);
        Serial.printf("btn %s: %s\n", buttonName(pressedButton), statusMenuTitle());
      }
      if (statusVisible) statusMenuTick();
      return;
    }

    if (statusVisible) {
      if (!statusMenuHandleButton(pressedButton)) {
        statusVisible = false;
        tft.fillScreen(TFT_BLACK);
        Serial.printf("btn %s: face\n", buttonName(pressedButton));
      } else {
        Serial.printf("btn %s: %s\n", buttonName(pressedButton), statusMenuTitle());
      }
      statusMenuTick();
      return;
    }

    statusVisible = true;
    statusMenuOpenMain();
    Serial.printf("btn %s: %s\n", buttonName(pressedButton), statusMenuTitle());
    return;
  }

  if (statusVisible) {
    drainLogWhileMenu();
    statusMenuTick();
    return;
  }

  {
    float petX = 0, petY = 0;
    const bool resting =
        faceMood() == TAMAFI_MOOD_SLEEP || petStateSnapshot().activity == PET_ACTIVITY_REST;
    const float tiltX = mpuOk() ? mpuTiltX() : 0;
    const float tiltY = mpuOk() ? mpuTiltY() : 0;
    const bool userLook = mpuOk() && (fabsf(tiltX) + fabsf(tiltY)) > 0.20f;
    if (resting) {
      faceClearLook();
    } else if (userLook && canSteerLook()) {
      faceSetLook(-tiltX * 22.0f, tiltY * 16.0f);
    } else if (petStateLook(petX, petY)) {
      faceSetLook(petX, petY);
    }
  }

  if (mpuOk()) {
    const TamafiGesture g = mpuConsumeGesture();
    if (g == TAMAFI_GESTURE_SHAKE) {
      petStateOnShaken();
      eventLogPush(LOG_IMU, "shake");
      imuTakeMood(TAMAFI_MOOD_DIZZY, 3400);
    } else if (g == TAMAFI_GESTURE_NUDGE) {
      petStateOnNudged();
      eventLogPush(LOG_IMU, "nudge");
      imuTakeMood(petStateSnapshot().happiness > 60 ? TAMAFI_MOOD_HAPPY : TAMAFI_MOOD_SHY, 1100);
    } else if (g == TAMAFI_GESTURE_TAP) {
      eventLogPush(LOG_IMU, "tap");
      imuTakeMood(TAMAFI_MOOD_SURPRISED, 1400);
    } else if (g == TAMAFI_GESTURE_TILT_HOLD) {
      petStateOnTiltHeld();
      eventLogPush(LOG_IMU, "tilt-hold");
      imuTakeMood(TAMAFI_MOOD_THINK, 1600);
    } else if (g == TAMAFI_GESTURE_FLIP) {
      eventLogPush(LOG_IMU, "flip");
      imuTakeMood(TAMAFI_MOOD_CONFUSED, 1800);
    }

    if (imuOwnsMood && millis() > imuMoodUntil && !faceBusy()) {
      imuOwnsMood = false;
      faceSetMood(moodBeforeImu);
    }
  }

  if (micOk() && !imuOwnsMood && !petFeedbackSpeaking() && !micHeldOff()) {
    const bool hearing = micHearing();
    const bool listenTooLong = micOwnsMood && micListenAt && (millis() - micListenAt > 3200);

    if (hearing && !wasHearing && !micOwnsMood && !faceBusy()) {
      moodBeforeMic = faceMood();
      if (moodBeforeMic == TAMAFI_MOOD_SLEEP || moodBeforeMic == TAMAFI_MOOD_BLINK ||
          moodBeforeMic == TAMAFI_MOOD_LISTEN) {
        moodBeforeMic = TAMAFI_MOOD_IDLE;
      }
      micOwnsMood = true;
      petOwnsMood = false;
      previewOwnsMood = false;
      micListenAt = millis();
      faceClearLook();
      faceSetMood(TAMAFI_MOOD_LISTEN);
      eventLogPush(LOG_MIC, "listen");
      s_micLogged = true;
      Serial.println("mic: listen");
    } else if (micOwnsMood && !faceBusy() && (!hearing || listenTooLong)) {
      if (wasHearing || listenTooLong) {
        micOwnsMood = false;
        micListenAt = 0;
        petStateOnHeard();
        stampsNote(STAMP_HEARD);
        faceSetMood(petStateBaselineMood());
        Serial.println("mic: quiet");
      }
    }
    wasHearing = hearing;
    s_micLogged = hearing;
  }

  if (petOwnsMood && millis() > petMoodUntil && !imuOwnsMood && !micOwnsMood && !faceBusy()) {
    petOwnsMood = false;
    if (previewOwnsMood) {
      previewOwnsMood = false;
      faceSetMood(moodBeforePreview);
    } else {
      faceSetMood(petStateBaselineMood());
    }
  }

  PetAction petAction;
  if (!imuOwnsMood && !micOwnsMood) {
    const bool urgent = petStateHasNeed();
    if ((urgent || (!petOwnsMood && !faceBusy())) &&
        petStateConsumeAction(petAction)) {
      petTakeAction(petAction);
    }
  }
}
