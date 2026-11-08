#include "pet_state.h"

#include "wifi_pet.h"

#include <Preferences.h>

static const uint32_t kDecayIntervalMs = 60000;
static const uint32_t kAgeIntervalMs = 60000;
static uint32_t s_saveIntervalMs = 30000;
static const char* kPrefsName = "tamafi_pet";

enum ActionPri : uint8_t {
  PRI_NONE = 0,
  PRI_FIDGET = 1,
  PRI_NOTICE = 2,
  PRI_DOING = 3,
  PRI_RESULT = 4,
  PRI_NEED = 5
};

static Preferences s_prefs;
static PetSnapshot s_pet = {42, 78, 70, 72, 45, 75, 0, PET_STAGE_BABY, PET_ACTIVITY_IDLE};
static PetAction s_pending = {TAMAFI_MOOD_IDLE, 0, nullptr};
static ActionPri s_pendingPri = PRI_NONE;
static bool s_hasAction = false;
static bool s_dirty = false;
static bool s_prefsOk = false;
static bool s_hatched = false;
static uint32_t s_lastDecayAt = 0;
static uint32_t s_lastAgeAt = 0;
static uint32_t s_lastSaveAt = 0;
static uint32_t s_nextDecisionAt = 0;
static uint32_t s_activityUntil = 0;
static uint32_t s_noticeUntil = 0;
static uint32_t s_airUntil = 0;
static int s_lastWifiCount = -1;
static int s_lastBleCount = -1;
static uint8_t s_failStreak = 0;
static PetActivity s_lastActivity = PET_ACTIVITY_IDLE;
static PetSignalStats s_wifi = {0, 0, 0, 0, -100};
static PetSignalStats s_ble = {0, 0, 0, 0, -100};
static float s_lookX = 0;
static float s_lookY = 0;
static uint32_t s_nextGlanceAt = 0;
static uint32_t s_nextBeatAt = 0;
static float s_huntConf = 0.5f;
static float s_discoverConf = 0.5f;
static float s_restConf = 0.5f;
static uint8_t s_energyBeforeRest = 78;
static bool s_lastFailed = false;
static const float kMemoryAlpha = 0.30f;

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static void rememberOutcome(float& conf, float outcome) {
  outcome = clampf(outcome, 0.0f, 1.0f);
  conf += kMemoryAlpha * (outcome - conf);
  conf = clampf(conf, 0.05f, 0.95f);
}

static int memoryBias(float conf, int swing) { return (int)((conf - 0.5f) * 2.0f * (float)swing); }

static uint8_t clampStat(int v) {
  if (v < 0) return 0;
  if (v > 100) return 100;
  return (uint8_t)v;
}

static void adjust(uint8_t& stat, int delta) {
  stat = clampStat((int)stat + delta);
  s_dirty = true;
}

static void queueAction(TamafiMood mood, uint16_t holdMs, const char* label, ActionPri pri) {
  if (s_hasAction && pri < s_pendingPri) return;
  s_pending = {mood, holdMs, label};
  s_pendingPri = pri;
  s_hasAction = true;
}

static void setActivity(PetActivity activity) {
  if (s_pet.activity == activity) return;
  s_pet.activity = activity;
  s_dirty = true;
}

static void scheduleDecision(uint32_t now, uint32_t minMs, uint32_t maxMs) {
  if (maxMs <= minMs) maxMs = minMs + 1;
  s_nextDecisionAt = now + random(minMs, maxMs);
}

static int rfCrowd() { return s_wifi.count + s_wifi.strongCount + s_ble.count + s_ble.strongCount; }

static uint32_t crowdPause() {
  const int n = rfCrowd();
  if (n >= 22) return 8000;
  if (n >= 14) return 5000;
  if (n >= 7) return 2500;
  return 0;
}

static uint16_t faceHold(uint16_t base) {
  uint32_t h = (uint32_t)base + crowdPause() / 3;
  if (h > 8000) h = 8000;
  return (uint16_t)h;
}

static uint32_t capu(uint32_t v, uint32_t hi) { return v > hi ? hi : v; }

static void scheduleAfterMemory(uint32_t now, float conf, bool failed) {
  uint32_t minMs;
  uint32_t maxMs;
  const uint32_t pause = crowdPause();
  if (failed) {
    minMs = 8000 + (uint32_t)((1.0f - conf) * 2500.0f);
    maxMs = 14000 + (uint32_t)((1.0f - conf) * 4000.0f);
  } else {
    minMs = 5000 + (uint32_t)((1.0f - conf) * 3000.0f);
    maxMs = 10000 + (uint32_t)((1.0f - conf) * 5000.0f);
  }
  minMs += pause;
  maxMs += pause + pause / 2;
  if (maxMs < minMs + 1) maxMs = minMs + 1;
  scheduleDecision(now, minMs, maxMs);
  Serial.printf("pet memory: hunt=%.2f disc=%.2f rest=%.2f hesitate=%u-%ums crowd=%d\n", s_huntConf,
                s_discoverConf, s_restConf, (unsigned)minMs, (unsigned)maxMs, rfCrowd());
}

static PetStage stageForAge(uint32_t ageMinutes) {
  if (ageMinutes >= 180) return PET_STAGE_ELDER;
  if (ageMinutes >= 60) return PET_STAGE_ADULT;
  if (ageMinutes >= 20) return PET_STAGE_TEEN;
  return PET_STAGE_BABY;
}

static void updateStage() {
  const PetStage next = stageForAge(s_pet.ageMinutes);
  if (next == s_pet.stage) return;
  s_pet.stage = next;
  adjust(s_pet.happiness, 8);
  adjust(s_pet.health, 4);
  queueAction(TAMAFI_MOOD_EXCITED, 2200, "evolve", PRI_NEED);
}

static int signalFoodScore() {
  int score = s_wifi.count * 2 + s_wifi.strongCount * 4;
  score += s_ble.count + s_ble.strongCount * 3;
  if (s_wifi.avgRSSI > -72) score += 4;
  if (s_ble.avgRSSI > -70) score += 3;
  return score;
}

static int randw(int lo, int hi) {
  if (hi <= lo) return lo;
  return (int)random(lo, hi);
}

static PetActivity pickWeighted(int hunt, int discover, int rest, int idle) {
  if (hunt < 0) hunt = 0;
  if (discover < 0) discover = 0;
  if (rest < 0) rest = 0;
  if (idle < 0) idle = 0;
  const int total = hunt + discover + rest + idle;
  if (total <= 0) return PET_ACTIVITY_IDLE;
  int roll = randw(0, total);
  if ((roll -= hunt) < 0) return PET_ACTIVITY_HUNT;
  if ((roll -= discover) < 0) return PET_ACTIVITY_DISCOVER;
  if ((roll -= rest) < 0) return PET_ACTIVITY_REST;
  return PET_ACTIVITY_IDLE;
}

static void fidget() {
  TamafiMood mood = TAMAFI_MOOD_IDLE;
  const char* label = "fidget";
  uint16_t hold = faceHold((uint16_t)randw(1800, 3200));

  const int roll = randw(0, 100);
  if (s_pet.happiness > 78 && roll < 28) {
    mood = (roll & 1) ? TAMAFI_MOOD_WINK : TAMAFI_MOOD_LAUGH;
    label = "play";
    if (mood == TAMAFI_MOOD_WINK) hold = 1200;
  } else if (s_pet.curiosity > 64 && roll < 40) {
    mood = TAMAFI_MOOD_THINK;
    label = "wonder";
  } else if (s_pet.hunger > 70 && roll < 35) {
    mood = TAMAFI_MOOD_SAD;
    label = "peckish";
  } else if (s_pet.happiness < 36 && roll < 45) {
    mood = (s_wifi.count == 0) ? TAMAFI_MOOD_BORED : TAMAFI_MOOD_SHY;
    label = "lonely";
  } else if (roll < 18) {
    mood = TAMAFI_MOOD_SHY;
    label = "shy";
  } else if (roll < 30) {
    mood = TAMAFI_MOOD_CONFUSED;
    label = "huh";
  } else if (s_pet.happiness > 60 && roll < 42) {
    mood = TAMAFI_MOOD_HAPPY;
    label = "content";
  } else {
    mood = TAMAFI_MOOD_IDLE;
    label = "idle";
    hold = faceHold((uint16_t)randw(1400, 2400));
  }
  queueAction(mood, hold, label, PRI_FIDGET);
}

static void updateGlance(uint32_t now) {
  if (s_pet.activity == PET_ACTIVITY_REST) {
    s_lookX = 0;
    s_lookY = 0;
    return;
  }
  if (now < s_nextGlanceAt) return;
  if (s_pet.activity == PET_ACTIVITY_HUNT) {
    s_lookX = (float)randw(-18, 19);
    s_lookY = (float)randw(-12, 13);
    s_nextGlanceAt = now + (uint32_t)randw(260, 640);
  } else if (s_pet.activity == PET_ACTIVITY_DISCOVER) {
    s_lookX = (float)randw(-14, 15);
    s_lookY = (float)randw(-10, 8);
    s_nextGlanceAt = now + (uint32_t)randw(480, 1100);
  } else {
    s_lookX = (float)randw(-7, 8);
    s_lookY = (float)randw(-5, 6);
    s_nextGlanceAt = now + (uint32_t)randw(1200, 2800);
  }
}

static void pulseActivity(uint32_t now) {
  if (s_pet.activity == PET_ACTIVITY_IDLE || s_pet.activity == PET_ACTIVITY_REST) return;
  if (now < s_nextBeatAt) return;
  s_nextBeatAt = now + (uint32_t)randw(4000, 7000) + crowdPause() / 2;
}

static void beginHunt(uint32_t now) {
  setActivity(PET_ACTIVITY_HUNT);
  const int food = signalFoodScore();
  uint32_t ms = (uint32_t)randw(5500, 11000) + crowdPause();
  ms = capu(ms, 16000);
  s_activityUntil = now + ms;
  s_nextBeatAt = now + 4000;
  s_nextGlanceAt = now;
  adjust(s_pet.energy, -3);
  queueAction(food > 10 ? TAMAFI_MOOD_EXCITED : TAMAFI_MOOD_THINK, (uint16_t)capu(ms + 400, 16000),
              "hunt-start", PRI_DOING);
  Serial.printf("pet decide: HUNT food=%d for %ums conf=%.2f crowd=%d\n", food, (unsigned)ms,
                s_huntConf, rfCrowd());
}

static void beginDiscover(uint32_t now) {
  setActivity(PET_ACTIVITY_DISCOVER);
  uint32_t ms = (uint32_t)randw(5000, 10000) + crowdPause();
  ms = capu(ms, 15000);
  s_activityUntil = now + ms;
  s_nextBeatAt = now + 4000;
  s_nextGlanceAt = now;
  adjust(s_pet.energy, -2);
  queueAction(TAMAFI_MOOD_THINK, (uint16_t)capu(ms + 400, 15000), "discover-start", PRI_DOING);
  Serial.printf("pet decide: DISCOVER for %ums conf=%.2f crowd=%d\n", (unsigned)ms, s_discoverConf,
                rfCrowd());
}

static void beginRest(uint32_t now) {
  setActivity(PET_ACTIVITY_REST);
  s_energyBeforeRest = s_pet.energy;
  uint32_t ms = (uint32_t)randw(8000, 16000);
  if (s_pet.stage == PET_STAGE_BABY || s_pet.stage == PET_STAGE_ELDER) ms += 2500;
  s_activityUntil = now + ms;
  queueAction(TAMAFI_MOOD_SLEEP, ms > 20000 ? 20000 : (uint16_t)ms, "rest-start", PRI_DOING);
  Serial.printf("pet decide: REST for %ums conf=%.2f\n", (unsigned)ms, s_restConf);
}

static void resolveHunt() {
  const int food = signalFoodScore();
  const bool failed = food <= 1;
  if (failed) {
    s_failStreak++;
    adjust(s_pet.hunger, 4);
    adjust(s_pet.happiness, -4);
    adjust(s_pet.health, -1);
    rememberOutcome(s_huntConf, 0.08f);
    s_lastFailed = true;
    queueAction(s_failStreak >= 2 ? TAMAFI_MOOD_ANGRY : TAMAFI_MOOD_SAD, faceHold(2600), "hunt-fail",
                PRI_RESULT);
  } else {
    s_failStreak = 0;
    adjust(s_pet.hunger, -min(22, food / 2 + 4));
    adjust(s_pet.happiness, min(10, food / 4 + 2));
    adjust(s_pet.health, s_wifi.strongCount > 2 || s_ble.strongCount > 1 ? 3 : 1);
    rememberOutcome(s_huntConf, clampf((float)food / 28.0f, 0.35f, 1.0f));
    s_lastFailed = false;
    queueAction(food > 24 ? TAMAFI_MOOD_EXCITED : TAMAFI_MOOD_HAPPY, faceHold(2800), "hunt-ok",
                PRI_RESULT);
  }
}

static void resolveDiscover() {
  int discovery = s_wifi.hiddenCount * 5 + s_wifi.openCount * 4 + s_wifi.count;
  discovery += s_ble.count * 2 + s_ble.strongCount * 4;
  if (discovery <= 1) {
    adjust(s_pet.curiosity, -3);
    adjust(s_pet.happiness, -2);
    rememberOutcome(s_discoverConf, 0.12f);
    s_lastFailed = true;
    queueAction(TAMAFI_MOOD_BORED, faceHold(2400), "discover-empty", PRI_RESULT);
  } else {
    adjust(s_pet.curiosity, min(14, discovery / 2 + 3));
    adjust(s_pet.happiness, min(8, discovery / 4 + 1));
    rememberOutcome(s_discoverConf, clampf((float)discovery / 20.0f, 0.35f, 1.0f));
    s_lastFailed = false;
    queueAction(discovery > 18 ? TAMAFI_MOOD_SURPRISED : TAMAFI_MOOD_HAPPY, faceHold(2800),
                "discover-ok", PRI_RESULT);
  }
}

static void resolveRest() {
  adjust(s_pet.energy, 12 + randw(0, 8));
  adjust(s_pet.health, 4);
  adjust(s_pet.comfort, 5);
  adjust(s_pet.hunger, 2);
  float outcome = 0.72f;
  if (s_energyBeforeRest > 70) outcome = 0.42f;
  else if (s_energyBeforeRest < 30) outcome = 0.88f;
  rememberOutcome(s_restConf, outcome);
  s_lastFailed = false;
  queueAction(TAMAFI_MOOD_HAPPY, faceHold(2200), "woke", PRI_RESULT);
}

static void finishActivity(uint32_t now) {
  const PetActivity done = s_pet.activity;
  s_lastActivity = done;
  switch (done) {
    case PET_ACTIVITY_HUNT:
      resolveHunt();
      break;
    case PET_ACTIVITY_DISCOVER:
      resolveDiscover();
      break;
    case PET_ACTIVITY_REST:
      resolveRest();
      break;
    default:
      break;
  }
  setActivity(PET_ACTIVITY_IDLE);
  s_activityUntil = 0;
  if (done == PET_ACTIVITY_HUNT) {
    scheduleAfterMemory(now, s_huntConf, s_lastFailed);
  } else if (done == PET_ACTIVITY_DISCOVER) {
    scheduleAfterMemory(now, s_discoverConf, s_lastFailed);
  } else if (done == PET_ACTIVITY_REST) {
    scheduleAfterMemory(now, s_restConf, false);
  } else {
    scheduleDecision(now, 2500, 7000);
  }
}

static void decideNextActivity(uint32_t now) {
  if (s_pet.activity != PET_ACTIVITY_IDLE) {
    updateGlance(now);
    pulseActivity(now);
    if (now >= s_activityUntil) finishActivity(now);
    return;
  }
  updateGlance(now);
  if (now < s_nextDecisionAt) return;

  int hunt = (int)s_pet.hunger * 2 + s_pet.curiosity / 4 + signalFoodScore() / 2 + randw(0, 18);
  int discover = (int)s_pet.curiosity + s_wifi.hiddenCount * 10 + s_wifi.openCount * 5 +
                 s_ble.count * 3 + randw(0, 22);
  int rest = (100 - (int)s_pet.energy) * 2 + (100 - (int)s_pet.health) + randw(0, 16);
  int idle = 22 + (int)s_pet.happiness / 3 + randw(0, 20);

  if (s_pet.hunger > 76) hunt += 24;
  if (s_pet.energy < 28) rest += 28;
  if (s_pet.health < 36) rest += 18;
  if (s_pet.curiosity > 70 && s_wifi.count + s_ble.count > 0) discover += 14;
  if (s_wifi.count == 0 && s_ble.count == 0) {
    hunt -= 12;
    discover -= 8;
    idle += 10;
  }
  const int crowd = rfCrowd();
  if (crowd >= 7) {
    idle += 16 + crowd;
    if (s_pet.hunger < 55) hunt -= 14;
    if (s_pet.curiosity < 60) discover -= 10;
  }

  switch (s_pet.stage) {
    case PET_STAGE_BABY:
      rest += 16;
      hunt -= 8;
      break;
    case PET_STAGE_TEEN:
      discover += 16;
      idle += 8;
      break;
    case PET_STAGE_ADULT:
      hunt += 10;
      break;
    case PET_STAGE_ELDER:
      rest += 20;
      hunt -= 10;
      break;
    default:
      break;
  }

  if (s_lastActivity == PET_ACTIVITY_HUNT) hunt /= 3;
  if (s_lastActivity == PET_ACTIVITY_DISCOVER) discover /= 3;
  if (s_lastActivity == PET_ACTIVITY_REST && s_pet.energy > 55) rest /= 3;

  hunt += memoryBias(s_huntConf, 28);
  discover += memoryBias(s_discoverConf, 22);
  rest += memoryBias(s_restConf, 18);
  rest += (int)((1.0f - s_huntConf) * 12.0f);
  discover += (int)((1.0f - s_huntConf) * 6.0f);
  idle += (int)((1.0f - s_huntConf) * 8.0f);
  if (hunt < 0) hunt = 0;
  if (discover < 0) discover = 0;
  if (rest < 0) rest = 0;
  if (idle < 0) idle = 0;

  PetActivity choice = pickWeighted(hunt, discover, rest, idle);
  const int flipChance = crowdPause() > 0 ? 8 : 18;
  if (randw(0, 100) < flipChance && choice != PET_ACTIVITY_IDLE) {
    const PetActivity other = pickWeighted(hunt, discover, rest, idle);
    if (other != choice) {
      Serial.printf("pet decide: changed mind %s -> %s\n", petActivityName(choice),
                    petActivityName(other));
      choice = other;
    }
  }

  switch (choice) {
    case PET_ACTIVITY_HUNT:
      beginHunt(now);
      break;
    case PET_ACTIVITY_DISCOVER:
      beginDiscover(now);
      break;
    case PET_ACTIVITY_REST:
      beginRest(now);
      break;
    default:
      fidget();
      {
        float drive = s_huntConf;
        if (s_pet.hunger > 70) drive = (drive + 1.0f) * 0.5f;
        uint32_t minMs = 4000 + (uint32_t)((1.0f - drive) * 4000.0f) + crowdPause();
        uint32_t maxMs = 8000 + (uint32_t)((1.0f - drive) * 8000.0f) + crowdPause();
        if (s_pet.hunger > 80 || s_pet.energy < 24) {
          if (minMs > 4000) minMs = 4000;
          if (maxMs > 8000) maxMs = 8000;
        }
        scheduleDecision(now, minMs, maxMs);
      }
      break;
  }
}

static void maybeNoticeScan(bool wifi, const PetSignalStats& stats, int lastCount) {
  const uint32_t now = millis();
  if (s_pet.activity != PET_ACTIVITY_IDLE) return;
  if (now < s_noticeUntil) return;

  const int delta = lastCount < 0 ? 0 : abs(stats.count - lastCount);
  const bool crowded = stats.count >= 8;
  const bool bigShift = !crowded && (delta >= 3 || stats.strongCount >= 3);
  const bool hugeShift = crowded && delta >= 8;
  if (crowded && !hugeShift) return;

  const int chance = s_pet.curiosity / 4 + (bigShift || hugeShift ? 28 : 8);
  if (randw(0, 100) >= chance) return;

  s_noticeUntil = now + (uint32_t)randw(crowded ? 22000 : 14000, crowded ? 40000 : 24000);
  if (stats.count <= 0) {
    if (randw(0, 100) < 40)
      queueAction(TAMAFI_MOOD_BORED, faceHold(1800), wifi ? "quiet-wifi" : "quiet-ble", PRI_NOTICE);
    return;
  }
  if (bigShift || hugeShift) {
    queueAction(TAMAFI_MOOD_SURPRISED, faceHold(1800), wifi ? "wifi-change" : "ble-change", PRI_NOTICE);
  } else if (stats.hiddenCount > 0 || stats.openCount > 0) {
    queueAction(TAMAFI_MOOD_THINK, faceHold(1800), "strange-signal", PRI_NOTICE);
  } else if (s_pet.happiness > 55) {
    queueAction(TAMAFI_MOOD_HAPPY, faceHold(1600), wifi ? "wifi-snack" : "ble-near", PRI_NOTICE);
  }
}

static void saveIfNeeded(uint32_t now, bool force = false) {
  if (!s_prefsOk || !s_dirty) return;
  if (!force && now - s_lastSaveAt < s_saveIntervalMs) return;

  s_prefs.putUChar("hunger", s_pet.hunger);
  s_prefs.putUChar("energy", s_pet.energy);
  s_prefs.putUChar("happy", s_pet.happiness);
  s_prefs.putUChar("health", s_pet.health);
  s_prefs.putUChar("curious", s_pet.curiosity);
  s_prefs.putUChar("comfort", s_pet.comfort);
  s_prefs.putULong("ageMin", s_pet.ageMinutes);
  s_prefs.putUChar("stage", (uint8_t)s_pet.stage);
  s_lastSaveAt = now;
  s_dirty = false;
}

bool petStateInit() {
  s_prefsOk = s_prefs.begin(kPrefsName, false);
  if (s_prefsOk) {
    s_pet.hunger = s_prefs.getUChar("hunger", s_pet.hunger);
    s_pet.energy = s_prefs.getUChar("energy", s_pet.energy);
    s_pet.happiness = s_prefs.getUChar("happy", s_pet.happiness);
    s_pet.health = s_prefs.getUChar("health", s_pet.health);
    s_pet.curiosity = s_prefs.getUChar("curious", s_pet.curiosity);
    s_pet.comfort = s_prefs.getUChar("comfort", s_pet.comfort);
    s_pet.ageMinutes = s_prefs.getULong("ageMin", s_pet.ageMinutes);
    s_pet.stage = (PetStage)s_prefs.getUChar("stage", (uint8_t)stageForAge(s_pet.ageMinutes));
    s_hatched = s_prefs.getBool("hatched", false);
  }

  randomSeed((uint32_t)(micros() ^ ESP.getFreeHeap() ^ millis()));
  s_lastDecayAt = millis();
  s_lastAgeAt = millis();
  s_lastSaveAt = millis();
  scheduleDecision(millis(), 4000, 9000);
  Serial.printf("Pet: hunger=%u energy=%u happy=%u health=%u curious=%u comfort=%u age=%u stage=%s\n",
                s_pet.hunger, s_pet.energy, s_pet.happiness, s_pet.health, s_pet.curiosity,
                s_pet.comfort, (unsigned)s_pet.ageMinutes, petStageName(s_pet.stage));
  return true;
}

void petStateUpdate() {
  const uint32_t now = millis();

  if (now - s_lastDecayAt >= kDecayIntervalMs) {
    s_lastDecayAt = now;
    adjust(s_pet.hunger, 1 + (randw(0, 100) < 40 ? 1 : 0));
    adjust(s_pet.energy, randw(0, 100) < 70 ? -1 : 0);
    adjust(s_pet.curiosity, randw(0, 100) < 55 ? -1 : 0);
    if (s_pet.hunger > 72) adjust(s_pet.happiness, -2);
    if (s_pet.hunger > 84) adjust(s_pet.health, -1);
    if (s_pet.energy < 28) adjust(s_pet.comfort, -1);
    if (s_pet.comfort < 24) adjust(s_pet.health, -1);

    if (s_pet.activity == PET_ACTIVITY_IDLE) {
      if (s_pet.energy < 16) {
        queueAction(TAMAFI_MOOD_SLEEP, 2200, "tired", PRI_NEED);
      } else if (s_pet.hunger > 88) {
        queueAction(TAMAFI_MOOD_SAD, 1800, "hungry", PRI_NEED);
      }
    }
  }

  if (now - s_lastAgeAt >= kAgeIntervalMs) {
    s_lastAgeAt = now;
    s_pet.ageMinutes++;
    s_dirty = true;
    updateStage();
  }

  decideNextActivity(now);
  saveIfNeeded(now);
}

void petStateApplyWifi(const PetSignalStats& stats) {
  const int prev = s_lastWifiCount;
  s_wifi = stats;
  s_lastWifiCount = stats.count;
  if (s_pet.activity == PET_ACTIVITY_HUNT) adjust(s_pet.energy, -1);
  if (stats.count > 0 && s_pet.activity == PET_ACTIVITY_IDLE) {
    adjust(s_pet.curiosity, stats.strongCount > 0 ? 2 : 1);
    if (stats.strongCount >= 2) adjust(s_pet.hunger, -1);
  } else if (stats.count <= 0) {
    adjust(s_pet.curiosity, -1);
  }
  maybeNoticeScan(true, stats, prev);

  const uint32_t now = millis();
  const bool acting = s_pet.activity != PET_ACTIVITY_IDLE;
  if (now >= s_airUntil) {
    const AirThreat threat = (AirThreat)stats.threat;
    if (acting && threat != AIR_HOSTILE && threat != AIR_NOISY) {

    } else {
    switch (threat) {
      case AIR_HOSTILE:
        adjust(s_pet.comfort, -8);
        adjust(s_pet.happiness, -4);
        adjust(s_pet.curiosity, 5);
        queueAction(TAMAFI_MOOD_AIR_ATTACK, faceHold(4200), "air-attack", PRI_NEED);
        s_airUntil = now + 18000 + crowdPause();
        Serial.printf("pet air: ATTACK deauth=%u\n", (unsigned)stats.deauth);
        break;
      case AIR_NOISY:
        adjust(s_pet.curiosity, 4);
        adjust(s_pet.comfort, -2);
        queueAction(TAMAFI_MOOD_AIR_NOISE, faceHold(3600), "air-noisy", PRI_NEED);
        s_airUntil = now + 16000 + crowdPause();
        Serial.printf("pet air: NOISY probes=%u\n", (unsigned)stats.probe);
        break;
      case AIR_OPEN:
        adjust(s_pet.comfort, -2);
        queueAction(TAMAFI_MOOD_AIR_OPEN, faceHold(2600), "air-open", PRI_NOTICE);
        s_airUntil = now + 28000 + crowdPause();
        break;
      case AIR_BUSY:
        adjust(s_pet.curiosity, 2);
        if (randw(0, 100) < 12) queueAction(TAMAFI_MOOD_THINK, faceHold(1800), "air-busy", PRI_FIDGET);
        s_airUntil = now + 22000 + crowdPause();
        break;
      default:
        break;
    }
    }
  }
}

void petStateApplyBle(const PetSignalStats& stats) {
  const int prev = s_lastBleCount;
  s_ble = stats;
  s_lastBleCount = stats.count;
  if (stats.count > 0 && s_pet.activity == PET_ACTIVITY_IDLE) {
    adjust(s_pet.curiosity, 1);
    adjust(s_pet.happiness, stats.strongCount > 0 ? 1 : 0);
  }
  maybeNoticeScan(false, stats, prev);
}

bool petStateConsumeAction(PetAction& action) {
  if (!s_hasAction) return false;
  action = s_pending;
  s_hasAction = false;
  s_pendingPri = PRI_NONE;
  return true;
}

bool petStateHasNeed() { return s_hasAction && s_pendingPri >= PRI_NEED; }

bool petStateLook(float& x, float& y) {
  if (s_pet.activity == PET_ACTIVITY_REST) return false;
  x = s_lookX;
  y = s_lookY;
  return true;
}

void petStateOnPetted() {
  adjust(s_pet.happiness, 7);
  adjust(s_pet.comfort, 8);
  adjust(s_pet.hunger, -1);
  queueAction(s_pet.happiness > 48 ? TAMAFI_MOOD_LOVE : TAMAFI_MOOD_HAPPY, 2200, "petted", PRI_NEED);
}

void petStateOnHeard() {
  adjust(s_pet.curiosity, 6);
  adjust(s_pet.happiness, 2);
  if (s_pet.activity == PET_ACTIVITY_REST) return;
  queueAction(s_pet.curiosity > 55 ? TAMAFI_MOOD_THINK : TAMAFI_MOOD_SHY, 1500, "heard-you", PRI_NOTICE);
}

void petStateOnShaken() {
  adjust(s_pet.comfort, -7);
  adjust(s_pet.happiness, -3);
  adjust(s_pet.energy, -2);
}

void petStateOnNudged() {
  adjust(s_pet.curiosity, 3);
  adjust(s_pet.happiness, 2);
  adjust(s_pet.comfort, 1);
}

void petStateOnTiltHeld() {
  adjust(s_pet.curiosity, 4);
}

TamafiMood petStateBaselineMood() {
  if (s_pet.activity == PET_ACTIVITY_REST) return TAMAFI_MOOD_SLEEP;
  if (s_pet.activity == PET_ACTIVITY_HUNT) return TAMAFI_MOOD_THINK;
  if (s_pet.activity == PET_ACTIVITY_DISCOVER) return TAMAFI_MOOD_THINK;
  if (s_pet.health < 22) return TAMAFI_MOOD_CRY;
  if (s_pet.hunger > 86) return TAMAFI_MOOD_SAD;
  if (s_pet.energy < 18) return TAMAFI_MOOD_SLEEP;
  if (s_pet.happiness < 28) return TAMAFI_MOOD_BORED;
  if (s_pet.curiosity > 72) return TAMAFI_MOOD_THINK;
  if (s_pet.happiness > 78) return TAMAFI_MOOD_HAPPY;
  return TAMAFI_MOOD_IDLE;
}

void petStateReset(bool fullReset) {
  s_pet.hunger = 70;
  s_pet.energy = 78;
  s_pet.happiness = 70;
  s_pet.health = 70;
  s_pet.curiosity = 45;
  s_pet.comfort = 75;
  s_pet.activity = PET_ACTIVITY_IDLE;
  if (fullReset) {
    s_pet.ageMinutes = 0;
    s_pet.stage = PET_STAGE_BABY;
    s_hatched = false;
    if (s_prefsOk) s_prefs.putBool("hatched", false);
  }
  s_hasAction = false;
  s_pendingPri = PRI_NONE;
  s_activityUntil = 0;
  s_lastActivity = PET_ACTIVITY_IDLE;
  s_failStreak = 0;
  s_huntConf = 0.5f;
  s_discoverConf = 0.5f;
  s_restConf = 0.5f;
  s_lastFailed = false;
  s_dirty = true;
  scheduleDecision(millis(), 3000, 7000);
  saveIfNeeded(millis(), true);
}

void petStateSetSaveIntervalMs(uint32_t ms) {
  if (ms < 10000) ms = 10000;
  if (ms > 180000) ms = 180000;
  s_saveIntervalMs = ms;
}

bool petStateHatched() { return s_hatched; }

void petStateMarkHatched() {
  if (s_hatched) return;
  s_hatched = true;
  if (s_prefsOk) s_prefs.putBool("hatched", true);
}

const PetSnapshot& petStateSnapshot() { return s_pet; }

const PetSignalStats& petStateWifi() { return s_wifi; }

const PetSignalStats& petStateBle() { return s_ble; }

const char* petStageName(PetStage stage) {
  switch (stage) {
    case PET_STAGE_BABY:
      return "baby";
    case PET_STAGE_TEEN:
      return "teen";
    case PET_STAGE_ADULT:
      return "adult";
    case PET_STAGE_ELDER:
      return "elder";
    default:
      return "unknown";
  }
}

const char* petActivityName(PetActivity activity) {
  switch (activity) {
    case PET_ACTIVITY_IDLE:
      return "idle";
    case PET_ACTIVITY_HUNT:
      return "hunt";
    case PET_ACTIVITY_DISCOVER:
      return "discover";
    case PET_ACTIVITY_REST:
      return "rest";
    default:
      return "unknown";
  }
}
