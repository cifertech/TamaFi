#include "status_screen.h"

#include "ble_pet.h"
#include "config.h"
#include "event_log.h"
#include "face.h"
#include "pet_feedback.h"
#include "pet_state.h"
#include "stamps.h"
#include "ui_glcd.h"
#include "ui_icons.h"
#include "wifi_pet.h"

#include <Preferences.h>
#include <string.h>

static const int kMenuCount = 8;
static const int kCtlCount = 5;
static const int kSetCount = 6;
static const uint32_t kAnimMs = 8;
static const int kAnimStep = 12;

static const int kW = 240;
static const int kHeaderH = 30;
static const int kFooterH = 14;
static const int kFooterY = kW - kFooterH;
static const int kBodyTop = 38;
static const uint8_t kScale = GLCD_L;
static const uint8_t kMid = GLCD_M;
static const uint8_t kSmall = GLCD_S;
static const int kEdge = 4;
static const int kPad = 8;
static const int kIcon = 16;
static const int kItemX = 36;
static const int kLabelX = 12;
static const int kMenuTop = 36;
static const int kMenuRow = 23;
static const int kMenuHiH = 22;
static const int kCtlTop = 40;
static const int kCtlRow = 32;
static const int kSetTop = 38;
static const int kSetRow = 29;
static const int kValRight = kW - 6;

static const uint16_t kTeal = 0x034E;
static const uint16_t kCyan = 0x067A;
static const uint16_t kDeep = 0x2A2A;
static const uint16_t kCoral = 0xE36A;
static const uint16_t kSand = 0xF50C;
static const uint16_t kGold = 0xE60D;
static const uint16_t kHiFill = kDeep;
static const uint16_t kMuted = 0x54D6;
static const uint16_t kIdle = kCoral;
static const uint16_t kBarBg = 0x1925;
static const uint16_t kOkGreen = kCyan;
static const uint16_t kWarn = kSand;
static const uint16_t kHint = kIdle;
static const uint16_t kRule = kTeal;
static const uint32_t kSleepAfterMs = 45000;
static const uint8_t kThemeCount = 5;

struct UiTheme {
  const char* name;
  uint16_t accent;
  uint16_t accent2;
};

static const UiTheme kThemes[kThemeCount] = {
    {"PIXEL", kCyan, kTeal},
    {"AMBER", kSand, kGold},
    {"LIME", kTeal, kCyan},
    {"ICE", kGold, kSand},
    {"ROSE", kCoral, kSand},
};

static uint8_t s_theme = 0;
static uint16_t accent() { return kThemes[s_theme % kThemeCount].accent; }
static uint16_t accent2() { return kThemes[s_theme % kThemeCount].accent2; }
static const char* themeName() { return kThemes[s_theme % kThemeCount].name; }

static StatusPage s_page = STATUS_MENU;
static bool s_fromHome = false;
static int s_menuIndex = 0;
static int s_ctlIndex = 0;
static int s_setIndex = 0;
static int s_menuHiY = kMenuTop;
static int s_ctlHiY = kCtlTop;
static int s_setHiY = kSetTop;
static uint32_t s_lastAnim = 0;
static uint32_t s_lastLive = 0;
static uint8_t s_logScroll = 0;
static uint8_t s_stampIndex = 0;
static uint8_t s_tftBri = 1;
static bool s_autoSleep = false;
static uint16_t s_autoSaveMs = 30000;
static bool s_asleep = false;
static uint32_t s_lastInputAt = 0;
static Preferences s_prefs;
static bool s_prefsOk = false;

static int rowY(int index, int top, int step) { return top + index * step; }

static TFT_eSPI* s_direct = nullptr;

static TFT_eSPI& d() {
  TFT_eSprite* spr = faceSprite();
  if (spr) return *spr;
  return *s_direct;
}

static void present() {
  if (faceSprite()) facePresentSprite();
}

static void textAt(int x, int y, const char* s, uint16_t fg, uint16_t bg = TFT_BLACK, uint8_t scale = kScale) {
  if (!s) return;
  glcdDrawText(d(), x, y, s, fg, bg, scale);
}

static void textRight(int xRight, int y, const char* s, uint16_t fg, uint16_t bg = TFT_BLACK, uint8_t scale = kScale) {
  if (!s) return;
  textAt(xRight - glcdTextW(s, scale), y, s, fg, bg, scale);
}

static void textCenter(int y, const char* s, uint16_t fg, uint16_t bg = TFT_BLACK, uint8_t scale = kScale) {
  if (!s) return;
  textAt((kW - glcdTextW(s, scale)) / 2, y, s, fg, bg, scale);
}

static void applyTftBrightness() {
  if (s_asleep) {
    ledcWrite(0, 0);
    return;
  }
  const uint8_t val = (s_tftBri == 0) ? 60 : (s_tftBri == 1) ? 150 : 255;
  ledcWrite(0, val);
}

static void saveUi() {
  if (!s_prefsOk) return;
  s_prefs.putUChar("tftBri", s_tftBri);
  s_prefs.putUChar("ledBri", petFeedbackLedBrightness());
  s_prefs.putBool("sound", petFeedbackSoundEnabled());
  s_prefs.putBool("neo", petFeedbackLedsEnabled());
  s_prefs.putBool("sleepOn", s_autoSleep);
  s_prefs.putUShort("saveMs", s_autoSaveMs);
  s_prefs.putUChar("theme", s_theme);
}

static const char* moodLabel() {
  const PetSnapshot& pet = petStateSnapshot();
  if (pet.health < 25) return "SICK";
  if (pet.hunger > 75) return "HUNGRY";
  if (pet.happiness > 80 && wifiPetStats().netCount > 8) return "EXCITED";
  if (pet.happiness > 60) return "HAPPY";
  if (wifiPetStats().netCount == 0) return "BORED";
  TamafiMood m = faceMood();
  if (m == TAMAFI_MOOD_THINK) return "CURIOUS";
  return "CALM";
}

static const char* stageLabel(PetStage stage) {
  switch (stage) {
    case PET_STAGE_BABY: return "BABY";
    case PET_STAGE_TEEN: return "TEEN";
    case PET_STAGE_ADULT: return "ADULT";
    case PET_STAGE_ELDER: return "ELDER";
    default: return "?";
  }
}

static const char* briText(uint8_t index) {
  return index == 0 ? "LOW" : index == 1 ? "MID" : "HIGH";
}

static uint16_t moodColor() {
  const PetSnapshot& pet = petStateSnapshot();
  if (pet.health < 25) return kCoral;
  if (pet.hunger > 75) return kWarn;
  if (pet.happiness > 60) return kOkGreen;
  return accent();
}

static void drawHeader(const char* title, const char* right = nullptr, uint16_t rightCol = 0) {
  TFT_eSPI& g = d();
  if (rightCol == 0) rightCol = accent();
  g.fillRect(0, 0, kW, kHeaderH, TFT_BLACK);
  g.fillRect(8, 8, 12, 12, TFT_WHITE);
  g.fillRect(10, 10, 8, 8, TFT_BLACK);
  g.fillRect(12, 12, 4, 4, accent());
  textAt(28, 7, title, TFT_WHITE);
  if (right && right[0]) textRight(kValRight, 10, right, rightCol, TFT_BLACK, kSmall);
  g.fillRect(0, kHeaderH - 2, kW, 1, accent());
  g.fillRect(0, kHeaderH - 1, kW, 1, accent2());
}

static void drawFooter(const char* hint) {
  TFT_eSPI& g = d();
  g.fillRect(0, kFooterY, kW, kFooterH, TFT_BLACK);
  g.drawFastHLine(kEdge + 4, kFooterY, kW - kEdge * 2 - 4, kRule);
  textAt(kEdge + 4, kFooterY + 4, hint, kHint, TFT_BLACK, kSmall);
}

static void drawHighlight(int y, int h) {
  TFT_eSPI& g = d();
  g.fillRect(kEdge, y, kW - kEdge * 2, h, kHiFill);
  g.fillRect(kEdge, y, 3, h, accent());
}

static void drawRowIcon(int id, int x, int y, uint16_t c) {
  uiDrawIconAt(d(), (uint8_t)id, x, y, kIcon, c);
}

static void drawCtlIcon(int id, int x, int y, uint16_t c) {
  static const uint8_t kIcons[] = {
      UI_ICON_SUN, UI_ICON_LED, UI_ICON_MIC, UI_ICON_PIXELS, UI_ICON_BACK};
  if (id < 0 || id >= (int)(sizeof(kIcons) / sizeof(kIcons[0]))) return;
  uiDrawIconAt(d(), kIcons[id], x, y, kIcon, c);
}

static void drawSetIcon(int id, int x, int y, uint16_t c) {
  static const uint8_t kIcons[] = {
      UI_ICON_PALETTE, UI_ICON_SLEEP, UI_ICON_SAVE, UI_ICON_RECYCLE, UI_ICON_NUKE,
      UI_ICON_BACK};
  if (id < 0 || id >= (int)(sizeof(kIcons) / sizeof(kIcons[0]))) return;
  uiDrawIconAt(d(), kIcons[id], x, y, kIcon, c);
}

static const int kCardX = 4;
static const int kCardW = 232;
static const int kSecHead = 22;
static const int kSecRow = 18;
static const int kSecGap = 4;
static const int kSecPad = 3;

static int sectionH(int rows, int extra = 0) {
  return kSecHead + rows * kSecRow + extra + kSecPad;
}

static void drawSection(int y, int h, const char* title) {
  TFT_eSPI& g = d();
  g.fillRect(kCardX, y, kCardW, h, kHiFill);
  g.fillRect(kCardX, y, 3, h, accent());
  textAt(kLabelX, y + 5, title, accent(), kHiFill, kMid);
  g.drawFastHLine(kLabelX, y + kSecHead - 4, kValRight - kLabelX, kRule);
}

static void drawKv(int y, const char* label, const char* value, uint16_t valueColor = TFT_WHITE) {
  textAt(kLabelX, y + 3, label, kIdle, kHiFill, kMid);
  textRight(kValRight, y + 3, value, valueColor, kHiFill, kMid);
}

static int drawBar(int y, const char* label, uint8_t pct, uint16_t barColor) {
  TFT_eSPI& g = d();
  char num[8];
  snprintf(num, sizeof(num), "%u%%", pct);
  textAt(kLabelX, y, label, kIdle, kHiFill, kMid);
  textRight(kValRight, y, num, barColor, kHiFill, kMid);

  const int pipN = 14;
  const int gap = 2;
  const int pipH = 4;
  const int track = kValRight - kLabelX;
  const int pipW = (track - (pipN - 1) * gap) / pipN;
  const int used = pipN * pipW + (pipN - 1) * gap;
  int x = kLabelX + (track - used) / 2;
  const int by = y + glcdCharH(kMid) + 3;
  int filled = ((int)pct * pipN + 50) / 100;
  if (pct > 0 && filled == 0) filled = 1;
  if (pct == 0) filled = 0;

  for (int i = 0; i < pipN; ++i) {
    g.fillRect(x, by, pipW, pipH, i < filled ? barColor : kBarBg);
    x += pipW + gap;
  }
  return y + glcdCharH(kMid) + 3 + pipH + 3;
}

static void animateY(int& pos, int target) {
  const uint32_t now = millis();
  if (now - s_lastAnim < kAnimMs) return;
  s_lastAnim = now;
  if (pos == target) return;
  const int diff = target - pos;
  const int step = diff > 0 ? kAnimStep : -kAnimStep;
  if (abs(diff) < kAnimStep) pos = target;
  else pos += step;
}

static void screenMenu() {
  TFT_eSPI& g = d();
  g.fillScreen(TFT_BLACK);
  drawHeader("MAIN MENU");

  const int target = rowY(s_menuIndex, kMenuTop, kMenuRow);
  animateY(s_menuHiY, target);
  drawHighlight(s_menuHiY, kMenuHiH);

  static const char* items[] = {
      "PET STATUS", "ENVIRONMENT", "SYSTEM", "CONTROLS", "SETTINGS", "RX LOG", "STAMPS", "BACK"};
  for (int i = 0; i < kMenuCount; ++i) {
    const int y = rowY(i, kMenuTop, kMenuRow);
    const bool sel = i == s_menuIndex;
    const uint16_t fg = sel ? kCyan : kIdle;
    const uint16_t bg = sel ? kHiFill : TFT_BLACK;
    const int iconY = y + (kMenuHiH - kIcon) / 2;
    const int textY = y + (kMenuHiH - glcdCharH(kScale)) / 2;
    drawRowIcon(i, kPad, iconY, sel ? accent() : kMuted);
    textAt(kItemX, textY, items[i], fg, bg);
    textRight(kValRight, textY, i == 7 ? "<" : ">", sel ? accent() : kMuted, bg);
  }
  drawFooter("UP/DN move   OK select");
  present();
}

static void screenPet() {
  TFT_eSPI& g = d();
  const PetSnapshot& pet = petStateSnapshot();
  const uint32_t age = pet.ageMinutes;
  char line[24];
  g.fillScreen(TFT_BLACK);
  drawHeader("PET STATUS", petActivityName(pet.activity), accent());

  const int boxY = kBodyTop;
  const int boxH = sectionH(3, 88);
  drawSection(boxY, boxH, "VITALS");
  int y = boxY + kSecHead;
  drawKv(y, "Stage", stageLabel(pet.stage), kCyan);
  y += kSecRow;
  snprintf(line, sizeof(line), "%lud %02luh", (unsigned long)(age / 1440), (unsigned long)((age / 60) % 24));
  drawKv(y, "Age", line);
  y += kSecRow;
  drawKv(y, "Mood", moodLabel(), moodColor());
  y += kSecRow + 2;
  y = drawBar(y, "Hunger", pet.hunger, pet.hunger > 75 ? kCoral : accent());
  y = drawBar(y, "Happy", pet.happiness, pet.happiness > 60 ? kOkGreen : accent());
  y = drawBar(y, "Energy", pet.energy, pet.energy < 30 ? kWarn : accent());
  drawBar(y, "Health", pet.health, pet.health < 25 ? kCoral : kOkGreen);

  drawFooter(s_fromHome ? "OK menu  RIGHT again" : "OK back");
  present();
}

static void screenEnv() {
  TFT_eSPI& g = d();
  const WifiPetStats& wifi = wifiPetStats();
  const BlePetStats& ble = blePetStats();
  char line[16];
  g.fillScreen(TFT_BLACK);
  drawHeader("ENVIRONMENT", airThreatName(wifi.threat),
             wifi.threat == AIR_HOSTILE ? kCoral
             : wifi.threat == AIR_CALM  ? kOkGreen
                                        : kWarn);

  const int wifiY = kBodyTop;
  const int wifiH = sectionH(4);
  drawSection(wifiY, wifiH, "WIFI");
  int y = wifiY + kSecHead;
  snprintf(line, sizeof(line), "%d", wifi.netCount);
  drawKv(y, "APs", line, accent());
  y += kSecRow;
  drawKv(y, "Air", airThreatName(wifi.threat),
         wifi.threat == AIR_HOSTILE ? kCoral
         : wifi.threat == AIR_NOISY || wifi.threat == AIR_OPEN ? kWarn
         : wifi.threat == AIR_CALM                              ? kOkGreen
                                                                : accent());
  y += kSecRow;
  snprintf(line, sizeof(line), "CH %02u", (unsigned)wifiPetChannel());
  drawKv(y, "Channel", line);
  y += kSecRow;
  if (wifi.avgRSSI > -100) snprintf(line, sizeof(line), "%d dBm", wifi.avgRSSI);
  else strncpy(line, "--", sizeof(line));
  drawKv(y, "RSSI", line, wifi.avgRSSI > -60 ? kOkGreen : TFT_WHITE);

  const int bleY = wifiY + wifiH + kSecGap;
  drawSection(bleY, sectionH(3), "BLUETOOTH");
  y = bleY + kSecHead;
  snprintf(line, sizeof(line), "%d", ble.deviceCount);
  drawKv(y, "Nodes", line, accent());
  y += kSecRow;
  snprintf(line, sizeof(line), "%d", ble.strongCount);
  drawKv(y, "Strong", line, ble.strongCount > 0 ? kOkGreen : kIdle);
  y += kSecRow;
  if (ble.avgRSSI > -100) snprintf(line, sizeof(line), "%d dBm", ble.avgRSSI);
  else strncpy(line, "--", sizeof(line));
  drawKv(y, "RSSI", line);

  drawFooter(s_fromHome ? "OK menu  RIGHT again" : "OK back");
  present();
}

static void screenSys() {
  TFT_eSPI& g = d();
  char line[20];
  g.fillScreen(TFT_BLACK);
  snprintf(line, sizeof(line), "%uK", (unsigned)(ESP.getFreeHeap() / 1024));
  drawHeader("SYSTEM", line, ESP.getFreeHeap() < 50000 ? kCoral : accent());

  const int devY = kBodyTop;
  const int devH = sectionH(4);
  drawSection(devY, devH, "DEVICE");
  int y = devY + kSecHead;
  drawKv(y, "Firmware", "2.0");
  y += kSecRow;
  drawKv(y, "Board", "ESP32-S3");
  y += kSecRow;
  snprintf(line, sizeof(line), "%uK", (unsigned)(ESP.getFreeHeap() / 1024));
  drawKv(y, "Heap", line, ESP.getFreeHeap() < 50000 ? kCoral : kOkGreen);
  y += kSecRow;
  const unsigned long s = millis() / 1000;
  snprintf(line, sizeof(line), "%02lu:%02lu:%02lu", s / 3600UL, (s / 60UL) % 60UL, s % 60UL);
  drawKv(y, "Uptime", line);

  const int radY = devY + devH + kSecGap;
  drawSection(radY, sectionH(3), "RADIO");
  y = radY + kSecHead;
  snprintf(line, sizeof(line), "%s CH%02u",
           wifiPetListening() ? "WATCH" : wifiPetScanning() ? "SCAN" : "IDLE",
           (unsigned)wifiPetChannel());
  drawKv(y, "WiFi", line, wifiPetBusy() ? kOkGreen : kIdle);
  y += kSecRow;
  drawKv(y, "BLE", blePetScanning() ? "SCAN" : "IDLE", blePetScanning() ? kOkGreen : kIdle);
  y += kSecRow;
  snprintf(line, sizeof(line), "%uK", (unsigned)(ESP.getMinFreeHeap() / 1024));
  drawKv(y, "Min heap", line);

  drawFooter("OK back");
  present();
}

static void screenCtl() {
  TFT_eSPI& g = d();
  g.fillScreen(TFT_BLACK);
  drawHeader("CONTROLS");
  animateY(s_ctlHiY, rowY(s_ctlIndex, kCtlTop, kCtlRow));
  drawHighlight(s_ctlHiY, kMenuHiH);

  static const char* labels[] = {"SCREEN", "LEDS", "SOUND", "PIXELS", "BACK"};
  for (int i = 0; i < kCtlCount; ++i) {
    const int y = rowY(i, kCtlTop, kCtlRow);
    const bool sel = i == s_ctlIndex;
    const uint16_t fg = sel ? kCyan : kIdle;
    const uint16_t bg = sel ? kHiFill : TFT_BLACK;
    const int iconY = y + (kMenuHiH - kIcon) / 2;
    const int textY = y + (kMenuHiH - glcdCharH(kScale)) / 2;
    drawCtlIcon(i, kPad, iconY, sel ? accent() : kMuted);
    textAt(kItemX, textY, labels[i], fg, bg);
    const char* val = "";
    if (i == 0) val = briText(s_tftBri);
    else if (i == 1) val = briText(petFeedbackLedBrightness());
    else if (i == 2) val = petFeedbackSoundEnabled() ? "ON" : "OFF";
    else if (i == 3) val = petFeedbackLedsEnabled() ? "ON" : "OFF";
    else val = "<";
    const uint16_t vc = (i == 4) ? (sel ? accent() : kMuted)
                                 : (i >= 2 && val[1] == 'F') ? kMuted : accent();
    textRight(kValRight, textY, val, vc, bg);
  }
  drawFooter("OK change");
  present();
}

static void screenSet() {
  TFT_eSPI& g = d();
  char line[8];
  g.fillScreen(TFT_BLACK);
  drawHeader("SETTINGS");
  animateY(s_setHiY, rowY(s_setIndex, kSetTop, kSetRow));
  drawHighlight(s_setHiY, kMenuHiH);

  static const char* labels[] = {"THEME", "SLEEP", "AUTOSAVE", "RESET PET", "RESET ALL", "BACK"};
  for (int i = 0; i < kSetCount; ++i) {
    const int y = rowY(i, kSetTop, kSetRow);
    const bool sel = i == s_setIndex;
    const uint16_t fg = sel ? kCyan : kIdle;
    const uint16_t bg = sel ? kHiFill : TFT_BLACK;
    const int iconY = y + (kMenuHiH - kIcon) / 2;
    const int textY = y + (kMenuHiH - glcdCharH(kScale)) / 2;
    drawSetIcon(i, kPad, iconY, sel ? accent() : kMuted);
    textAt(kItemX, textY, labels[i], fg, bg);
    if (i == 0) textRight(kValRight, textY, themeName(), accent(), bg);
    else if (i == 1) textRight(kValRight, textY, s_autoSleep ? "ON" : "OFF", s_autoSleep ? accent() : kMuted, bg);
    else if (i == 2) {
      snprintf(line, sizeof(line), "%us", (unsigned)(s_autoSaveMs / 1000));
      textRight(kValRight, textY, line, accent(), bg);
    } else if (i == 5) {
      textRight(kValRight, textY, "<", sel ? accent() : kMuted, bg);
    }
  }
  drawFooter("OK change");
  present();
}

static const uint8_t kLogVis = 12;
static const int kHudY = 36;
static const int kHudH = 34;
static const int kLogTop = 74;
static const int kLogRowH = 12;

static uint8_t logMaxScroll() {
  const uint8_t n = eventLogCount();
  return n > kLogVis ? (uint8_t)(n - kLogVis) : 0;
}

static void formatLogAge(char* out, size_t n, uint32_t atMs) {
  const uint32_t dt = millis() - atMs;
  if (dt < 1400) {
    strncpy(out, "now", n);
    out[n - 1] = 0;
    return;
  }
  if (dt < 60000) {
    snprintf(out, n, "%us", (unsigned)(dt / 1000));
    return;
  }
  if (dt < 3600000UL) {
    snprintf(out, n, "%um", (unsigned)(dt / 60000UL));
    return;
  }
  snprintf(out, n, "%uh", (unsigned)(dt / 3600000UL));
}

static void screenLog() {
  TFT_eSPI& g = d();
  g.fillScreen(TFT_BLACK);

  g.fillRect(0, 0, kW, kHeaderH, TFT_BLACK);
  g.drawLine(14, 22, 14, 8, accent());
  g.fillTriangle(10, 8, 18, 8, 14, 4, accent());
  g.drawCircle(14, 10, 5, kCyan);
  textAt(28, 7, "RX LOG", TFT_WHITE);
  char seq[8];
  snprintf(seq, sizeof(seq), "#%03lu", (unsigned long)(eventLogSeq() % 1000UL));
  textAt(118, 10, seq, kMuted, TFT_BLACK, kSmall);
  const int nets = wifiPetStats().netCount;
  for (int i = 0; i < 4; ++i) {
    const int h = 3 + i * 3;
    const uint16_t c = (nets > i * 3) ? accent() : kDeep;
    g.fillRect(168 + i * 5, 22 - h, 3, h, c);
  }
  const bool liveOn = ((millis() / 380) % 2) == 0;
  textRight(kW - 6, 10, "LIVE", liveOn ? kCoral : kDeep, TFT_BLACK, kSmall);
  g.fillRect(0, kHeaderH - 2, kW, 1, accent());
  g.fillRect(0, kHeaderH - 1, kW, 1, accent2());

  const WifiPetStats& wifi = wifiPetStats();
  const BlePetStats& ble = blePetStats();
  const char* radio = wifiPetListening() ? "WATCH" : wifiPetScanning() ? "SCAN" : "IDLE";
  const AirThreat air = wifi.threat;
  const uint16_t airCol = air == AIR_HOSTILE ? kCoral
                          : air == AIR_CALM ? kOkGreen
                          : kWarn;
  const unsigned heapK = (unsigned)(ESP.getFreeHeap() / 1024);
  const unsigned long sec = millis() / 1000UL;
  char line[40];

  g.fillRect(0, kHudY, kW, kHudH, kHiFill);
  g.fillRect(0, kHudY, 3, kHudH, accent());

  snprintf(line, sizeof(line), "%-5s CH%02u  %dAP", radio, (unsigned)wifiPetChannel(), wifi.netCount);
  textAt(8, kHudY + 4, line, accent(), kHiFill, kSmall);
  if (wifi.avgRSSI > -100) {
    snprintf(line, sizeof(line), "%ddBm", wifi.avgRSSI);
  } else {
    strncpy(line, "--dBm", sizeof(line));
    line[sizeof(line) - 1] = 0;
  }
  textRight(kW - 6, kHudY + 4, line, wifi.avgRSSI > -60 ? kOkGreen : kIdle, kHiFill, kSmall);

  snprintf(line, sizeof(line), "BLE %d  DA %u  PR %u  HID %d", ble.deviceCount,
           (unsigned)wifi.deauth, (unsigned)wifi.probe, wifi.hiddenCount);
  textAt(8, kHudY + 14, line, kIdle, kHiFill, kSmall);

  snprintf(line, sizeof(line), "AIR %-6s  %uK  %02lu:%02lu:%02lu", airThreatName(air), heapK,
           sec / 3600UL, (sec / 60UL) % 60UL, sec % 60UL);
  textAt(8, kHudY + 24, line, airCol, kHiFill, kSmall);

  g.fillRect(0, kHudY + kHudH, kW, 1, kRule);

  if (s_logScroll > logMaxScroll()) s_logScroll = logMaxScroll();

  const uint8_t n = eventLogCount();
  if (n == 0) {
    textCenter(130, "NO PACKETS YET", kMuted, TFT_BLACK, kMid);
    textCenter(148, "waiting on air", kMuted, TFT_BLACK, kSmall);
  } else {
    for (uint8_t i = 0; i < kLogVis; ++i) {
      const uint8_t age = (uint8_t)(s_logScroll + i);
      const LogEntry* e = eventLogAt(age);
      if (!e) break;
      const int y = kLogTop + i * kLogRowH;
      const uint16_t tag = eventLogKindColor(e->kind);
      const uint16_t fg = (age == 0) ? TFT_WHITE : (age > 10 ? kMuted : kIdle);
      if (age == 0) g.fillRect(0, y - 1, 3, kLogRowH, accent());
      textAt(6, y, eventLogKindName(e->kind), tag, TFT_BLACK, kSmall);
      textAt(36, y, e->text, fg, TFT_BLACK, kSmall);
      char ageTxt[6];
      formatLogAge(ageTxt, sizeof(ageTxt), e->ms);
      textRight(kW - 4, y, ageTxt, kMuted, TFT_BLACK, kSmall);
    }
  }

  char hint[28];
  snprintf(hint, sizeof(hint), "UP/DN  OK back  %s", radio);
  drawFooter(s_fromHome ? "OK menu  RIGHT again" : hint);
  present();
}

static void screenStamps() {
  TFT_eSPI& g = d();
  char line[12];
  g.fillScreen(TFT_BLACK);
  snprintf(line, sizeof(line), "%u/%u", (unsigned)stampsUnlockedCount(), (unsigned)STAMP_COUNT);
  drawHeader("STAMPS", line, accent());

  const int cols = 4;
  const int cellW = 56;
  const int cellH = 52;
  const int x0 = 8;
  const int y0 = 40;
  if (s_stampIndex >= STAMP_COUNT) s_stampIndex = 0;

  for (uint8_t i = 0; i < STAMP_COUNT; ++i) {
    const int col = i % cols;
    const int row = i / cols;
    const int x = x0 + col * cellW;
    const int y = y0 + row * cellH;
    const bool sel = i == s_stampIndex;
    const bool have = stampsHas((StampId)i);
    const uint16_t box = sel ? kHiFill : TFT_BLACK;
    g.fillRect(x, y, cellW - 4, cellH - 4, box);
    if (sel) {
      g.fillRect(x, y, 3, cellH - 4, accent());
      g.drawRect(x, y, cellW - 4, cellH - 4, accent());
    }
    const uint16_t ic = have ? (sel ? kCyan : accent()) : kMuted;
    stampsDrawMark(g, (StampId)i, x + (cellW - 4) / 2, y + 16, 28, ic);
    const char* name = have ? stampsTitle((StampId)i) : "----";
    const int tw = glcdTextW(name, kSmall);
    textAt(x + ((cellW - 4) - tw) / 2, y + 36, name, have ? TFT_WHITE : kMuted, box, kSmall);
  }

  drawFooter(s_fromHome ? "OK menu  RIGHT again" : "UP/DN move  OK back");
  present();
}

static void drawCurrent() {
  switch (s_page) {
    case STATUS_MENU: screenMenu(); break;
    case STATUS_PET: screenPet(); break;
    case STATUS_ENV: screenEnv(); break;
    case STATUS_SYS: screenSys(); break;
    case STATUS_CTL: screenCtl(); break;
    case STATUS_SET: screenSet(); break;
    case STATUS_LOG: screenLog(); break;
    case STATUS_STAMPS: screenStamps(); break;
  }
}

static void goPage(StatusPage page, bool fromHome) {
  s_page = page;
  s_fromHome = fromHome;
  if (page == STATUS_MENU) s_menuHiY = rowY(s_menuIndex, kMenuTop, kMenuRow);
  if (page == STATUS_CTL) s_ctlHiY = rowY(s_ctlIndex, kCtlTop, kCtlRow);
  if (page == STATUS_SET) s_setHiY = rowY(s_setIndex, kSetTop, kSetRow);
  if (page == STATUS_LOG) s_logScroll = 0;
  if (page == STATUS_STAMPS) s_stampIndex = 0;
  drawCurrent();
}

static bool isUp(int pin) { return pin == PIN_BTN_UP; }
static bool isDown(int pin) { return pin == PIN_BTN_DOWN; }
static bool isOk(int pin) { return pin == PIN_BTN_OK; }

void statusMenuInit(TFT_eSPI& tft) {
  s_direct = &tft;
  ledcSetup(0, 12000, 8);
  ledcAttachPin(TFT_BL, 0);
  s_prefsOk = s_prefs.begin("tamafi_ui", false);
  if (s_prefsOk) {
    s_tftBri = s_prefs.getUChar("tftBri", 1);
    petFeedbackSetLedBrightness(s_prefs.getUChar("ledBri", 1));
    petFeedbackSetSoundEnabled(s_prefs.getBool("sound", true));
    petFeedbackSetLedsEnabled(s_prefs.getBool("neo", true));
    s_autoSleep = s_prefs.getBool("sleepOn", false);
    s_autoSaveMs = s_prefs.getUShort("saveMs", 30000);
    s_theme = s_prefs.getUChar("theme", 0);
    if (s_theme >= kThemeCount) s_theme = 0;
  }
  s_lastInputAt = millis();
  applyTftBrightness();
}

void statusMenuOpenMain() {
  s_menuIndex = 0;
  goPage(STATUS_MENU, false);
}

void statusMenuOpenQuick(StatusPage page) { goPage(page, true); }

StatusPage statusMenuPage() { return s_page; }

bool statusMenuHandleButton(int pin) {
  if (s_page == STATUS_MENU) {
    if (isUp(pin)) {
      s_menuIndex = (s_menuIndex - 1 + kMenuCount) % kMenuCount;
      drawCurrent();
      return true;
    }
    if (isDown(pin)) {
      s_menuIndex = (s_menuIndex + 1) % kMenuCount;
      drawCurrent();
      return true;
    }
    if (isOk(pin)) {
      switch (s_menuIndex) {
        case 0: goPage(STATUS_PET, false); break;
        case 1: goPage(STATUS_ENV, false); break;
        case 2: goPage(STATUS_SYS, false); break;
        case 3: goPage(STATUS_CTL, false); break;
        case 4: goPage(STATUS_SET, false); break;
        case 5: goPage(STATUS_LOG, false); break;
        case 6: goPage(STATUS_STAMPS, false); break;
        case 7: return false;
      }
      return true;
    }
    return true;
  }

  if (s_page == STATUS_LOG) {
    if (isUp(pin)) {
      if (s_logScroll > 0) s_logScroll--;
      drawCurrent();
      return true;
    }
    if (isDown(pin)) {
      if (s_logScroll < logMaxScroll()) s_logScroll++;
      drawCurrent();
      return true;
    }
    if (isOk(pin)) {
      goPage(STATUS_MENU, false);
      return true;
    }
    return true;
  }

  if (s_page == STATUS_STAMPS) {
    if (isUp(pin)) {
      s_stampIndex = (uint8_t)((s_stampIndex + STAMP_COUNT - 1) % STAMP_COUNT);
      drawCurrent();
      return true;
    }
    if (isDown(pin)) {
      s_stampIndex = (uint8_t)((s_stampIndex + 1) % STAMP_COUNT);
      drawCurrent();
      return true;
    }
    if (isOk(pin)) {
      goPage(STATUS_MENU, false);
      return true;
    }
    return true;
  }

  if (s_page == STATUS_CTL) {
    if (isUp(pin)) {
      s_ctlIndex = (s_ctlIndex - 1 + kCtlCount) % kCtlCount;
      drawCurrent();
      return true;
    }
    if (isDown(pin)) {
      s_ctlIndex = (s_ctlIndex + 1) % kCtlCount;
      drawCurrent();
      return true;
    }
    if (isOk(pin)) {
      switch (s_ctlIndex) {
        case 0:
          s_tftBri = (s_tftBri + 1) % 3;
          applyTftBrightness();
          saveUi();
          break;
        case 1:
          petFeedbackSetLedBrightness((petFeedbackLedBrightness() + 1) % 3);
          saveUi();
          break;
        case 2:
          petFeedbackSetSoundEnabled(!petFeedbackSoundEnabled());
          saveUi();
          break;
        case 3:
          petFeedbackSetLedsEnabled(!petFeedbackLedsEnabled());
          saveUi();
          break;
        case 4:
          goPage(STATUS_MENU, false);
          return true;
      }
      drawCurrent();
      return true;
    }
    return true;
  }

  if (s_page == STATUS_SET) {
    if (isUp(pin)) {
      s_setIndex = (s_setIndex - 1 + kSetCount) % kSetCount;
      drawCurrent();
      return true;
    }
    if (isDown(pin)) {
      s_setIndex = (s_setIndex + 1) % kSetCount;
      drawCurrent();
      return true;
    }
    if (isOk(pin)) {
      switch (s_setIndex) {
        case 0:
          s_theme = (uint8_t)((s_theme + 1) % kThemeCount);
          saveUi();
          break;
        case 1:
          s_autoSleep = !s_autoSleep;
          if (!s_autoSleep && s_asleep) {
            s_asleep = false;
            applyTftBrightness();
          }
          saveUi();
          break;
        case 2:
          if (s_autoSaveMs == 15000) s_autoSaveMs = 30000;
          else if (s_autoSaveMs == 30000) s_autoSaveMs = 60000;
          else s_autoSaveMs = 15000;
          saveUi();
          petStateSetSaveIntervalMs(s_autoSaveMs);
          break;
        case 3:
          petStateReset(false);
          break;
        case 4:
          petStateReset(true);
          break;
        case 5:
          goPage(STATUS_MENU, false);
          return true;
      }
      drawCurrent();
      return true;
    }
    return true;
  }

  if (isOk(pin)) {
    goPage(STATUS_MENU, false);
    return true;
  }
  return true;
}

void statusMenuTick() {
  if (!s_direct && !faceSprite()) return;
  const bool animating =
      (s_page == STATUS_MENU && s_menuHiY != rowY(s_menuIndex, kMenuTop, kMenuRow)) ||
      (s_page == STATUS_CTL && s_ctlHiY != rowY(s_ctlIndex, kCtlTop, kCtlRow)) ||
      (s_page == STATUS_SET && s_setHiY != rowY(s_setIndex, kSetTop, kSetRow));
  const uint32_t liveMs = (s_page == STATUS_LOG) ? 220 : 3000;
  const bool live = (s_page == STATUS_SYS || s_page == STATUS_LOG || s_page == STATUS_ENV ||
                     s_page == STATUS_PET) &&
                    (millis() - s_lastLive > liveMs);
  if (!animating && !live) return;
  if (live) s_lastLive = millis();
  drawCurrent();
}

const char* statusMenuTitle() {
  switch (s_page) {
    case STATUS_MENU: return "Main Menu";
    case STATUS_PET: return "Pet Status";
    case STATUS_ENV: return "Environment";
    case STATUS_SYS: return "System Info";
    case STATUS_CTL: return "Controls";
    case STATUS_SET: return "Settings";
    case STATUS_LOG: return "RX Log";
    case STATUS_STAMPS: return "Stamps";
    default: return "Menu";
  }
}

bool statusMenuAsleep() { return s_asleep; }

bool statusMenuWakeIfAsleep() {
  s_lastInputAt = millis();
  if (!s_asleep) return false;
  s_asleep = false;
  applyTftBrightness();
  Serial.println("ui: wake");
  return true;
}

void statusMenuPowerTick() {
  if (!s_autoSleep) {
    if (s_asleep) {
      s_asleep = false;
      applyTftBrightness();
    }
    return;
  }
  if (s_asleep) return;
  if (millis() - s_lastInputAt < kSleepAfterMs) return;
  s_asleep = true;
  ledcWrite(0, 0);
  Serial.println("ui: sleep");
}

void statusMenuSyncPet() { petStateSetSaveIntervalMs(s_autoSaveMs); }

void statusMenuSave() { saveUi(); }

bool statusMenuSleepEnabled() { return s_autoSleep; }

void statusMenuSetSleep(bool on) {
  s_autoSleep = on;
  if (!s_autoSleep && s_asleep) {
    s_asleep = false;
    applyTftBrightness();
  }
  saveUi();
}

const char* statusMenuThemeName() { return themeName(); }

uint8_t statusMenuThemeIndex() { return s_theme % kThemeCount; }

bool statusMenuSetTheme(uint8_t index) {
  if (index >= kThemeCount) return false;
  s_theme = index;
  saveUi();
  return true;
}

static bool nameEq(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    char ca = *a++;
    char cb = *b++;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
    if (ca != cb) return false;
  }
  return *a == *b;
}

bool statusMenuSetThemeName(const char* name) {
  if (!name || !name[0]) return false;
  if (name[0] >= '0' && name[0] <= '9' && name[1] == 0) {
    return statusMenuSetTheme((uint8_t)(name[0] - '0'));
  }
  for (uint8_t i = 0; i < kThemeCount; ++i) {
    if (nameEq(name, kThemes[i].name)) return statusMenuSetTheme(i);
  }
  return false;
}

uint16_t statusMenuSaveIntervalMs() { return s_autoSaveMs; }

void statusMenuSetSaveIntervalMs(uint16_t ms) {
  if (ms <= 20000) s_autoSaveMs = 15000;
  else if (ms <= 45000) s_autoSaveMs = 30000;
  else s_autoSaveMs = 60000;
  petStateSetSaveIntervalMs(s_autoSaveMs);
  saveUi();
}

uint8_t statusMenuTftBrightness() { return s_tftBri; }

void statusMenuSetTftBrightness(uint8_t index) {
  if (index > 2) index = 2;
  s_tftBri = index;
  applyTftBrightness();
  saveUi();
}

const char* statusMenuMoodLabel() { return moodLabel(); }
