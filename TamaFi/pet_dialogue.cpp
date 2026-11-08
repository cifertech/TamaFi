#include "pet_dialogue.h"
#include "pet_state.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

struct EventSay {
  const char* key;
  uint8_t n;
  TamafiMood mood[6];
  const char* line[6];
};

static const EventSay kEvents[] = {
    {"hunt-start",
     6,
     {TAMAFI_MOOD_THINK, TAMAFI_MOOD_EXCITED, TAMAFI_MOOD_HAPPY, TAMAFI_MOOD_THINK,
      TAMAFI_MOOD_EXCITED, TAMAFI_MOOD_HAPPY},
     {"scanning rf", "hunting rssi", "lock a beacon", "need packets", "channel hop",
      "seek a signal"}},
    {"hunt-ok",
     6,
     {TAMAFI_MOOD_HAPPY, TAMAFI_MOOD_EXCITED, TAMAFI_MOOD_LAUGH, TAMAFI_MOOD_WINK,
      TAMAFI_MOOD_HAPPY, TAMAFI_MOOD_LAUGH},
     {"got a beacon", "rssi is good", "strong hit", "captured one", "locked on",
      "band paid off"}},
    {"hunt-fail",
     6,
     {TAMAFI_MOOD_SAD, TAMAFI_MOOD_ANGRY, TAMAFI_MOOD_BORED, TAMAFI_MOOD_CONFUSED,
      TAMAFI_MOOD_SAD, TAMAFI_MOOD_ANGRY},
     {"no beacons", "rssi too low", "empty band", "scan miss", "nothing locked",
      "dead channel"}},
    {"discover-start",
     6,
     {TAMAFI_MOOD_THINK, TAMAFI_MOOD_SURPRISED, TAMAFI_MOOD_THINK, TAMAFI_MOOD_EXCITED,
      TAMAFI_MOOD_SURPRISED, TAMAFI_MOOD_THINK},
     {"new emitter?", "unknown ping", "trace that", "rf anomaly", "ghost ssid?",
      "what's on air"}},
    {"discover-ok",
     6,
     {TAMAFI_MOOD_SURPRISED, TAMAFI_MOOD_HAPPY, TAMAFI_MOOD_EXCITED, TAMAFI_MOOD_THINK,
      TAMAFI_MOOD_LAUGH, TAMAFI_MOOD_HAPPY},
     {"new neighbor", "mapped a node", "unknown ap", "logged a net", "new fingerprint",
      "tagged a hit"}},
    {"discover-empty",
     6,
     {TAMAFI_MOOD_BORED, TAMAFI_MOOD_SAD, TAMAFI_MOOD_CONFUSED, TAMAFI_MOOD_BORED,
      TAMAFI_MOOD_SAD, TAMAFI_MOOD_BORED},
     {"quiet spectrum", "no clients", "null scan", "idle air", "nothing new",
      "same old band"}},
    {"woke",
     6,
     {TAMAFI_MOOD_HAPPY, TAMAFI_MOOD_IDLE, TAMAFI_MOOD_SHY, TAMAFI_MOOD_HAPPY,
      TAMAFI_MOOD_IDLE, TAMAFI_MOOD_SHY},
     {"radio up", "rx online", "syncing...", "back on air", "sensors live",
      "boot ok"}},
    {"heard-you",
     6,
     {TAMAFI_MOOD_THINK, TAMAFI_MOOD_SHY, TAMAFI_MOOD_SURPRISED, TAMAFI_MOOD_HAPPY,
      TAMAFI_MOOD_THINK, TAMAFI_MOOD_SHY},
     {"audio spike", "mic hit", "acoustic?", "not rf, you", "listening",
      "input locked"}},
    {"air-attack",
     6,
     {TAMAFI_MOOD_AIR_ATTACK, TAMAFI_MOOD_AIR_ATTACK, TAMAFI_MOOD_SCARED, TAMAFI_MOOD_AIR_ATTACK,
      TAMAFI_MOOD_SCARED, TAMAFI_MOOD_AIR_ATTACK},
     {"deauth storm", "hostile air", "disassoc hit", "mgmt attack", "kick frames",
      "air is hostile"}},
    {"air-noisy",
     6,
     {TAMAFI_MOOD_AIR_NOISE, TAMAFI_MOOD_AIR_NOISE, TAMAFI_MOOD_CONFUSED, TAMAFI_MOOD_AIR_NOISE,
      TAMAFI_MOOD_AIR_NOISE, TAMAFI_MOOD_CONFUSED},
     {"probe flood", "busy spectrum", "mgmt spam", "scan noise", "air saturated",
      "too many reqs"}},
    {"air-open",
     6,
     {TAMAFI_MOOD_AIR_OPEN, TAMAFI_MOOD_SHY, TAMAFI_MOOD_AIR_OPEN, TAMAFI_MOOD_THINK,
      TAMAFI_MOOD_SHY, TAMAFI_MOOD_THINK},
     {"open essid", "no encryption", "unsecured ap", "open beacon", "null crypto",
      "wpa missing"}},
    {"evolve",
     6,
     {TAMAFI_MOOD_EXCITED, TAMAFI_MOOD_SURPRISED, TAMAFI_MOOD_HAPPY, TAMAFI_MOOD_LAUGH,
      TAMAFI_MOOD_EXCITED, TAMAFI_MOOD_HAPPY},
     {"stage up", "new stage", "params shifted", "evolved", "form updated",
      "next stage"}},
};

static const uint8_t kEventCount = sizeof(kEvents) / sizeof(kEvents[0]);
static uint8_t s_last[kEventCount];
static bool s_lastInit = false;

static const EventSay* findEvent(const char* label, TamafiMood mood) {
  if (!label) return nullptr;
  if (strcmp(label, "air-preview") == 0) {
    if (mood == TAMAFI_MOOD_AIR_ATTACK) label = "air-attack";
    else if (mood == TAMAFI_MOOD_AIR_NOISE) label = "air-noisy";
    else if (mood == TAMAFI_MOOD_AIR_OPEN) label = "air-open";
  }
  for (uint8_t i = 0; i < kEventCount; ++i) {
    if (strcmp(kEvents[i].key, label) == 0) return &kEvents[i];
  }
  return nullptr;
}

static char s_fact[17];

static int cap99(int v) {
  if (v < 0) return 0;
  if (v > 99) return 99;
  return v;
}

static bool putn(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(s_fact, sizeof(s_fact), fmt, ap);
  va_end(ap);
  return n > 0 && n < (int)sizeof(s_fact);
}

static bool fillFact(const char* key) {
  const PetSnapshot& pet = petStateSnapshot();
  const PetSignalStats& w = petStateWifi();
  const PetSignalStats& b = petStateBle();
  const int ap = cap99(w.count);
  const int st = cap99(w.strongCount);
  const int op = cap99(w.openCount);
  const int hid = cap99(w.hiddenCount);
  const int ble = cap99(b.count);
  const int rssi = w.avgRSSI;
  const bool hasRssi = ap > 0 && rssi > -100 && rssi < 0;
  const unsigned deauth = (unsigned)cap99((int)w.deauth);
  const unsigned probes = (unsigned)cap99((int)w.probe);
  const uint8_t slot = (uint8_t)(esp_random() % 3);

  if (strcmp(key, "hunt-start") == 0) {
    if (slot == 0 && ap > 0) return putn("%d ap nearby", ap);
    if (slot == 1 && hasRssi) return putn("rssi %d", rssi);
    if (ap > 0) return putn("%d strong ap", st);
    return false;
  }
  if (strcmp(key, "hunt-ok") == 0) {
    if (pet.hunger > 78) {
      if (slot == 0) return putn("hunger %u", (unsigned)pet.hunger);
      if (slot == 1) return putn("%d ap, empty", ap);
      return putn("need rf %dap", ap);
    }
    if (slot == 0 && hasRssi) return putn("%d ap %ddbm", ap, rssi);
    if (slot == 1) return putn("%d strong ap", st);
    if (hasRssi) return putn("rssi %d", rssi);
    return putn("%d ap locked", ap);
  }
  if (strcmp(key, "hunt-fail") == 0) {
    if (ap == 0) return putn("0 beacons");
    if (slot == 0 && hasRssi) return putn("rssi %d", rssi);
    if (slot == 1) return putn("%d ap weak", ap);
    return putn("%d ap miss", ap);
  }
  if (strcmp(key, "discover-start") == 0) {
    if (slot == 0 && ble > 0) return putn("ble %d live", ble);
    if (slot == 1 && hid > 0) return putn("%d hidden?", hid);
    if (op > 0) return putn("%d open ap", op);
    if (ap > 0) return putn("%d ap nearby", ap);
    return false;
  }
  if (strcmp(key, "discover-ok") == 0) {
    if (slot == 0 && hid > 0) return putn("%d hidden", hid);
    if (slot == 1 && ble > 0) return putn("ble %d", ble);
    if (op > 0) return putn("%d open ap", op);
    return putn("wifi %d ble %d", ap, ble);
  }
  if (strcmp(key, "discover-empty") == 0) {
    if (slot == 0) return putn("wifi %d ble %d", ap, ble);
    if (slot == 1) return putn("idle, %d ap", ap);
    return putn("0 new nodes");
  }
  if (strcmp(key, "woke") == 0) {
    if (slot == 0) return putn("energy %u", (unsigned)pet.energy);
    if (slot == 1) return putn("hunger %u", (unsigned)pet.hunger);
    return putn("rx online");
  }
  if (strcmp(key, "air-attack") == 0) {
    if (deauth > 0) {
      if (slot & 1) return putn("deauth x%u", deauth);
      return putn("%u kick rx", deauth);
    }
    return false;
  }
  if (strcmp(key, "air-noisy") == 0) {
    if (probes > 0) {
      if (slot & 1) return putn("probes %u", probes);
      return putn("probe x%u", probes);
    }
    return false;
  }
  if (strcmp(key, "air-open") == 0) {
    if (op > 0) {
      if (slot & 1) return putn("%d open ap", op);
      return putn("open x%d", op);
    }
    return false;
  }
  if (strcmp(key, "evolve") == 0) {
    if (slot & 1) return putn("%s now", petStageName(pet.stage));
    return putn("stage %s", petStageName(pet.stage));
  }
  return false;
}

static uint8_t factChance(const char* key) {
  if (strcmp(key, "air-attack") == 0 || strcmp(key, "air-noisy") == 0 ||
      strcmp(key, "air-open") == 0)
    return 85;
  if (strcmp(key, "hunt-ok") == 0 || strcmp(key, "hunt-fail") == 0 ||
      strcmp(key, "discover-ok") == 0 || strcmp(key, "discover-empty") == 0)
    return 70;
  if (strcmp(key, "hunt-start") == 0 || strcmp(key, "discover-start") == 0 ||
      strcmp(key, "woke") == 0 || strcmp(key, "evolve") == 0)
    return 45;
  return 0;
}

bool petDialoguePick(const char* label, TamafiMood& mood, const char*& line) {
  line = nullptr;
  const EventSay* ev = findEvent(label, mood);
  if (!ev || ev->n == 0) return false;

  const uint8_t idx = (uint8_t)(ev - kEvents);
  if (!s_lastInit) {
    memset(s_last, 0xFF, sizeof(s_last));
    s_lastInit = true;
  }
  uint8_t pick = (uint8_t)(esp_random() % ev->n);
  if (ev->n > 1 && pick == s_last[idx]) pick = (uint8_t)((pick + 1) % ev->n);
  s_last[idx] = pick;

  if (strcmp(ev->key, "hunt-ok") == 0 && petStateSnapshot().hunger > 78) {
    mood = (pick & 1) ? TAMAFI_MOOD_SAD : TAMAFI_MOOD_THINK;
  } else {
    mood = ev->mood[pick];
  }

  const uint8_t chance = factChance(ev->key);
  if (chance && (uint8_t)(esp_random() % 100) < chance && fillFact(ev->key)) {
    line = s_fact;
    return true;
  }

  if (strcmp(ev->key, "hunt-ok") == 0 && petStateSnapshot().hunger > 78) {
    static const char* kStillHungry[] = {
        "need more rf", "thin harvest", "rssi not enough",
        "still empty", "one hit, weak", "need denser air"};
    line = kStillHungry[pick % 6];
    return true;
  }

  line = ev->line[pick];
  return true;
}
