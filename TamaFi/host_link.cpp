#include "host_link.h"
#include "tamafi_usb.h"

#include "event_log.h"
#include "face.h"
#include "pet_feedback.h"
#include "pet_state.h"
#include "stamps.h"
#include "status_screen.h"
#include "tamafi_pins.h"
#include "wifi_pet.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char s_line[64];
static uint8_t s_len = 0;
static int s_pendingKey = -1;

static bool ieq(const char* a, const char* b) {
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

static bool isPreviewChar(char c) {
  c = (char)tolower((unsigned char)c);
  return c == 'd' || c == 'p' || c == 'o' || c == 'l' || c == 'w';
}

static char* skipSpaces(char* s) {
  while (s && *s == ' ') s++;
  return s;
}

static void splitWord(char* line, char*& word, char*& rest) {
  word = skipSpaces(line);
  rest = nullptr;
  if (!word || !word[0]) {
    word = line;
    return;
  }
  char* p = word;
  while (*p && *p != ' ') p++;
  if (*p) {
    *p++ = 0;
    rest = skipSpaces(p);
    if (rest && !rest[0]) rest = nullptr;
  }
}

static bool parseOnOff(const char* s, bool& out) {
  if (!s || !s[0]) return false;
  if (ieq(s, "1") || ieq(s, "on") || ieq(s, "true") || ieq(s, "yes")) {
    out = true;
    return true;
  }
  if (ieq(s, "0") || ieq(s, "off") || ieq(s, "false") || ieq(s, "no")) {
    out = false;
    return true;
  }
  return false;
}

static const char* logKindTag(LogKind kind) {
  switch (kind) {
    case LOG_HUNT: return "HUNT";
    case LOG_DISC: return "DISC";
    case LOG_REST: return "REST";
    case LOG_AIR: return "AIR";
    case LOG_WIFI: return "WIFI";
    case LOG_BLE: return "BLE";
    case LOG_IMU: return "IMU";
    case LOG_MIC: return "MIC";
    case LOG_SYS: return "SYS";
    default: return "PET";
  }
}

static void replyOk() { TamafiHost.println("OK"); }

static void replyErr(const char* why) {
  TamafiHost.print("ERR ");
  TamafiHost.println(why);
}

static void cmdPing() {
  TamafiHost.println("PONG TAMAFI 1");
  replyOk();
}

static void cmdInfo() {
  TamafiHost.printf("INFO fw=face proto=1 heap=%u minheap=%u up_s=%lu\n", ESP.getFreeHeap(),
                ESP.getMinFreeHeap(), (unsigned long)(millis() / 1000UL));
  replyOk();
}

static void cmdStatus() {
  const PetSnapshot& p = petStateSnapshot();
  const PetSignalStats& w = petStateWifi();
  const PetSignalStats& b = petStateBle();
  const WifiPetStats& wifi = wifiPetStats();
  TamafiHost.printf(
      "STATUS hunger=%u energy=%u happy=%u health=%u curious=%u comfort=%u "
      "age=%lu stage=%s activity=%s mood=%s face=%s air=%s ch=%u aps=%d rssi=%d "
      "ble=%d deauth=%u probe=%u hidden=%d open=%d heap=%u sleep=%u theme=%s "
      "save=%u stamps=%u/%u sound=%u leds=%u tft=%u led=%u logseq=%lu\n",
      (unsigned)p.hunger, (unsigned)p.energy, (unsigned)p.happiness, (unsigned)p.health,
      (unsigned)p.curiosity, (unsigned)p.comfort, (unsigned long)p.ageMinutes,
      petStageName(p.stage), petActivityName(p.activity), statusMenuMoodLabel(),
      faceMoodName(faceMood()), airThreatName(wifi.threat), (unsigned)wifiPetChannel(),
      w.count, w.avgRSSI, b.count, (unsigned)w.deauth, (unsigned)w.probe, w.hiddenCount,
      w.openCount, ESP.getFreeHeap(), statusMenuSleepEnabled() ? 1u : 0u,
      statusMenuThemeName(), (unsigned)(statusMenuSaveIntervalMs() / 1000u),
      (unsigned)stampsUnlockedCount(), (unsigned)STAMP_COUNT,
      petFeedbackSoundEnabled() ? 1u : 0u, petFeedbackLedsEnabled() ? 1u : 0u,
      (unsigned)statusMenuTftBrightness(), (unsigned)petFeedbackLedBrightness(),
      (unsigned long)eventLogSeq());
  replyOk();
}

static void cmdStamps() {
  for (uint8_t i = 0; i < STAMP_COUNT; ++i) {
    TamafiHost.printf("STAMP %u %u %s\n", (unsigned)i, stampsHas((StampId)i) ? 1u : 0u,
                  stampsTitle((StampId)i));
  }
  replyOk();
}

static void cmdLog() {
  const uint8_t n = eventLogCount();
  for (uint8_t i = 0; i < n; ++i) {
    const LogEntry* e = eventLogAt(i);
    if (!e) break;
    TamafiHost.printf("LOG %lu %s %s\n", (unsigned long)e->ms, logKindTag(e->kind), e->text);
  }
  replyOk();
}

static bool queueKey(const char* name) {
  if (!name || !name[0]) return false;
  int pin = -1;
  if (ieq(name, "up") || ieq(name, "u")) pin = PIN_BTN_UP;
  else if (ieq(name, "ok") || ieq(name, "enter") || ieq(name, "select")) pin = PIN_BTN_OK;
  else if (ieq(name, "down")) pin = PIN_BTN_DOWN;
  else if (ieq(name, "right1") || ieq(name, "r1") || ieq(name, "pet")) pin = PIN_BTN_RIGHT1;
  else if (ieq(name, "right2") || ieq(name, "r2") || ieq(name, "env")) pin = PIN_BTN_RIGHT2;
  else if (ieq(name, "right3") || ieq(name, "r3") || ieq(name, "log")) pin = PIN_BTN_RIGHT3;
  if (pin < 0) return false;
  s_pendingKey = pin;
  return true;
}

int hostLinkTakeKey() {
  const int pin = s_pendingKey;
  s_pendingKey = -1;
  return pin;
}

static void cmdKey(char* rest) {
  char* name = nullptr;
  char* extra = nullptr;
  splitWord(rest, name, extra);
  if (!queueKey(name)) {
    replyErr("value");
    return;
  }
  replyOk();
}

static void cmdSet(char* rest) {
  char* key = nullptr;
  char* val = nullptr;
  splitWord(rest, key, val);
  if (!key || !key[0] || !val || !val[0]) {
    replyErr("args");
    return;
  }

  if (ieq(key, "sleep")) {
    bool on = false;
    if (!parseOnOff(val, on)) {
      replyErr("value");
      return;
    }
    statusMenuSetSleep(on);
    replyOk();
    return;
  }

  if (ieq(key, "theme")) {
    if (!statusMenuSetThemeName(val)) {
      replyErr("value");
      return;
    }
    replyOk();
    return;
  }

  if (ieq(key, "save")) {
    const long n = strtol(val, nullptr, 10);
    if (n <= 0) {
      replyErr("value");
      return;
    }
    uint16_t ms = (n >= 1000) ? (uint16_t)n : (uint16_t)(n * 1000L);
    statusMenuSetSaveIntervalMs(ms);
    replyOk();
    return;
  }

  if (ieq(key, "sound")) {
    bool on = false;
    if (!parseOnOff(val, on)) {
      replyErr("value");
      return;
    }
    petFeedbackSetSoundEnabled(on);
    statusMenuSave();
    replyOk();
    return;
  }

  if (ieq(key, "leds") || ieq(key, "neo")) {
    bool on = false;
    if (!parseOnOff(val, on)) {
      replyErr("value");
      return;
    }
    petFeedbackSetLedsEnabled(on);
    statusMenuSave();
    replyOk();
    return;
  }

  if (ieq(key, "tft")) {
    uint8_t bri = 1;
    if (ieq(val, "low")) bri = 0;
    else if (ieq(val, "mid") || ieq(val, "medium")) bri = 1;
    else if (ieq(val, "high")) bri = 2;
    else if (val[0] >= '0' && val[0] <= '2' && val[1] == 0) bri = (uint8_t)(val[0] - '0');
    else {
      replyErr("value");
      return;
    }
    statusMenuSetTftBrightness(bri);
    replyOk();
    return;
  }

  if (ieq(key, "led")) {
    uint8_t bri = 1;
    if (ieq(val, "low")) bri = 0;
    else if (ieq(val, "mid") || ieq(val, "medium")) bri = 1;
    else if (ieq(val, "high")) bri = 2;
    else if (val[0] >= '0' && val[0] <= '2' && val[1] == 0) bri = (uint8_t)(val[0] - '0');
    else {
      replyErr("value");
      return;
    }
    petFeedbackSetLedBrightness(bri);
    statusMenuSave();
    replyOk();
    return;
  }

  replyErr("key");
}

static void handleLine(char* line) {
  line = skipSpaces(line);
  if (!line || !line[0]) return;

  if (line[1] == 0) {
    if (isPreviewChar(line[0])) hostLinkOnPreview(line[0]);
    return;
  }

  char* cmd = nullptr;
  char* rest = nullptr;
  splitWord(line, cmd, rest);
  if (!cmd || !cmd[0]) return;

  if (ieq(cmd, "PING")) {
    cmdPing();
    return;
  }
  if (ieq(cmd, "STATUS")) {
    cmdStatus();
    return;
  }
  if (ieq(cmd, "STAMPS")) {
    cmdStamps();
    return;
  }
  if (ieq(cmd, "LOG")) {
    cmdLog();
    return;
  }
  if (ieq(cmd, "INFO")) {
    cmdInfo();
    return;
  }
  if (ieq(cmd, "SAVE")) {
    statusMenuSave();
    replyOk();
    return;
  }
  if (ieq(cmd, "SET")) {
    cmdSet(rest);
    return;
  }
  if (ieq(cmd, "KEY")) {
    cmdKey(rest);
    return;
  }
  if (ieq(cmd, "PREVIEW")) {
    if (!hostLinkOnPreviewName(rest)) {
      replyErr("value");
      return;
    }
    replyOk();
    return;
  }

  replyErr("cmd");
}

void hostLinkTick() {
  while (TamafiHost.available()) {
    const char c = (char)TamafiHost.read();
    if (c == '\r') continue;
    if (c == '\n') {
      s_line[s_len] = 0;
      if (s_len) handleLine(s_line);
      s_len = 0;
      return;
    }
    if (s_len == 0 && isPreviewChar(c) && !TamafiHost.available()) {
      hostLinkOnPreview(c);
      return;
    }
    if (s_len < sizeof(s_line) - 1) {
      s_line[s_len++] = c;
    } else {
      s_len = 0;
    }
  }
}
