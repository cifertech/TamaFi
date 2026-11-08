#include "config.h"
#include "wifi_pet.h"

#include <WiFi.h>
#if TAMAFI_AIR_WATCH_ENABLE
#include "esp_wifi.h"
#endif

static const uint32_t kFirstScanMs = 1800;
static const uint32_t kListenMs = 10000;
static const uint32_t kWindowMs = 2000;
static const uint32_t kYieldMs = 7000;
static const uint32_t kHopMs = 250;
static const uint32_t kStickyMs = 20000;
static const uint8_t kHop[] = {1, 6, 11, 1, 6, 11, 2, 7, 3, 8, 4, 9, 5, 10};

enum Phase : uint8_t { PHASE_IDLE = 0, PHASE_SCAN, PHASE_LISTEN, PHASE_YIELD };

static bool s_ok = false;
static bool s_radioReady = false;
static Phase s_phase = PHASE_IDLE;
static bool s_hasStats = false;
static uint32_t s_phaseUntil = 0;
static uint32_t s_nextHopAt = 0;
static uint32_t s_nextWindowAt = 0;
static uint32_t s_stickyUntil = 0;
static uint32_t s_noisyUntil = 0;
static uint8_t s_hopIdx = 0;
static bool s_nextIsScan = true;
static WifiPetStats s_stats = {0, 0, 0, 0, -100, 0, 0, AIR_CALM};

#if TAMAFI_AIR_WATCH_ENABLE
static volatile uint32_t s_deauth = 0;
static volatile uint32_t s_disassoc = 0;
static volatile uint32_t s_probe = 0;
static volatile uint32_t s_beacon = 0;

static void IRAM_ATTR onSniff(void* buf, wifi_promiscuous_pkt_type_t type) {
  (void)type;
  if (!buf) return;
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  if (pkt->rx_ctrl.sig_len < 2) return;
  const uint8_t fc0 = pkt->payload[0];
  const uint8_t ftype = (fc0 >> 2) & 0x03;
  const uint8_t subtype = (fc0 >> 4) & 0x0F;
  if (ftype != 0) return;
  if (subtype == 12) s_deauth++;
  else if (subtype == 10) s_disassoc++;
  else if (subtype == 4) s_probe++;
  else if (subtype == 8) s_beacon++;
}

static void stopListen() {
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
}

static bool startListen(uint32_t now) {
  const uint32_t heap = ESP.getFreeHeap();
  if (heap < TAMAFI_AIR_WATCH_MIN_FREE_HEAP) {
    Serial.printf("WiFi pet: listen skip heap=%u\n", (unsigned)heap);
    return false;
  }

  s_deauth = 0;
  s_disassoc = 0;
  s_probe = 0;
  s_beacon = 0;

  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_CTRL |
                       WIFI_PROMIS_FILTER_MASK_DATA;
  esp_err_t err = esp_wifi_set_promiscuous_filter(&filter);
  if (err != ESP_OK) {
    Serial.printf("WiFi pet: filter fail %d\n", (int)err);
    return false;
  }
  err = esp_wifi_set_promiscuous_rx_cb(&onSniff);
  if (err != ESP_OK) {
    Serial.printf("WiFi pet: cb fail %d\n", (int)err);
    return false;
  }
  err = esp_wifi_set_promiscuous(true);
  if (err != ESP_OK) {
    Serial.printf("WiFi pet: promisc fail %d\n", (int)err);
    return false;
  }

  s_hopIdx = 0;
  esp_wifi_set_channel(kHop[0], WIFI_SECOND_CHAN_NONE);
  s_phase = PHASE_LISTEN;
  s_phaseUntil = now + kListenMs;
  s_nextHopAt = now + kHopMs;
  s_nextWindowAt = now + kWindowMs;
  Serial.printf("WiFi pet: listening heap=%u\n", (unsigned)heap);
  return true;
}

static void publishWindow(uint32_t now) {
  const uint16_t deauth = (uint16_t)(s_deauth + s_disassoc);
  const uint16_t probe = (uint16_t)s_probe;
  s_deauth = 0;
  s_disassoc = 0;
  s_probe = 0;
  s_beacon = 0;

  s_stats.deauth = deauth;
  s_stats.probe = probe;

  AirThreat threat = AIR_CALM;
  if (deauth >= 2) threat = AIR_HOSTILE;
  else if (probe >= 10) threat = AIR_NOISY;
  else if (s_stats.openCount >= 3) threat = AIR_OPEN;
  else if (s_stats.netCount >= 12) threat = AIR_BUSY;

  if (threat == AIR_HOSTILE) s_stickyUntil = now + kStickyMs;
  if (threat == AIR_NOISY) s_noisyUntil = now + 14000;
  if (now < s_stickyUntil && threat < AIR_HOSTILE) threat = AIR_HOSTILE;
  else if (now < s_noisyUntil && threat < AIR_NOISY) threat = AIR_NOISY;

  s_stats.threat = threat;
  s_hasStats = true;
  Serial.printf("WiFi pet: air=%s deauth=%u probe=%u ch=%u\n", airThreatName(threat),
                (unsigned)deauth, (unsigned)probe, (unsigned)kHop[s_hopIdx]);
}
#endif

static bool ensureRadioReady() {
  if (s_radioReady) return true;
  Serial.println("WiFi pet: starting radio");
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  s_radioReady = true;
  Serial.println("WiFi pet: radio ready");
  return true;
}

static void startScan(uint32_t now) {
  if (!ensureRadioReady()) {
    s_phase = PHASE_IDLE;
    s_phaseUntil = now + 8000;
    return;
  }
#if TAMAFI_AIR_WATCH_ENABLE
  stopListen();
#endif
  WiFi.scanDelete();
  const int started = WiFi.scanNetworks(true, true);
  if (started == WIFI_SCAN_FAILED) {
    s_phase = PHASE_IDLE;
    s_phaseUntil = now + 8000;
    return;
  }
  s_phase = PHASE_SCAN;
}

const char* airThreatName(AirThreat threat) {
  switch (threat) {
    case AIR_BUSY:
      return "BUSY";
    case AIR_OPEN:
      return "OPEN";
    case AIR_NOISY:
      return "NOISY";
    case AIR_HOSTILE:
      return "ATTACK";
    default:
      return "CALM";
  }
}

bool wifiPetInit() {
  s_ok = true;
  ensureRadioReady();
  s_phase = PHASE_IDLE;
  s_phaseUntil = millis() + kFirstScanMs;
  Serial.println("WiFi pet: scan + air watch");
  return true;
}

void wifiPetUpdate(bool radioFree) {
  if (!s_ok) return;
  const uint32_t now = millis();

#if TAMAFI_AIR_WATCH_ENABLE
  if (s_phase == PHASE_LISTEN) {
    if (now >= s_nextHopAt) {
      if (s_probe >= 6) {
        s_nextHopAt = now + kHopMs;
      } else {
        s_hopIdx = (uint8_t)((s_hopIdx + 1) % (sizeof(kHop) / sizeof(kHop[0])));
        esp_wifi_set_channel(kHop[s_hopIdx], WIFI_SECOND_CHAN_NONE);
        s_nextHopAt = now + kHopMs;
      }
    }
    if (now >= s_nextWindowAt) {
      publishWindow(now);
      s_nextWindowAt = now + kWindowMs;
    }
    if (now < s_phaseUntil) return;
    stopListen();
    s_phase = PHASE_IDLE;
    s_phaseUntil = now;
    s_nextIsScan = true;
  }
#endif

  if (s_phase == PHASE_SCAN) {
    const int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    s_phase = PHASE_IDLE;
    if (n < 0) {
      WiFi.scanDelete();
      Serial.println("WiFi pet: scan failed");
    } else {
      s_stats.netCount = n;
      s_stats.strongCount = 0;
      s_stats.hiddenCount = 0;
      s_stats.openCount = 0;
      int totalRSSI = 0;
      for (int i = 0; i < n; ++i) {
        const int rssi = WiFi.RSSI(i);
        totalRSSI += rssi;
        if (rssi > -60) s_stats.strongCount++;
        if (WiFi.SSID(i).length() == 0) s_stats.hiddenCount++;
        if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) s_stats.openCount++;
      }
      s_stats.avgRSSI = n > 0 ? totalRSSI / n : -100;
      WiFi.scanDelete();
      Serial.printf("WiFi pet: nets=%d open=%d hidden=%d avg=%d\n", s_stats.netCount,
                    s_stats.openCount, s_stats.hiddenCount, s_stats.avgRSSI);
      s_hasStats = true;
    }
    s_phase = PHASE_YIELD;
    s_phaseUntil = now + kYieldMs;
    return;
  }

  if (s_phase == PHASE_YIELD) {
    if (now < s_phaseUntil) return;
    s_phase = PHASE_IDLE;
  }

  if (!radioFree || now < s_phaseUntil) return;

#if TAMAFI_AIR_WATCH_ENABLE
  if (s_nextIsScan) {
    s_nextIsScan = false;
    startScan(now);
  } else if (!startListen(now)) {
    s_nextIsScan = true;
    s_phaseUntil = now + 4000;
  }
#else
  startScan(now);
#endif
}

bool wifiPetConsumeStats(WifiPetStats& stats) {
  if (!s_hasStats) return false;
  stats = s_stats;
  s_hasStats = false;
  return true;
}

bool wifiPetScanning() { return s_phase == PHASE_SCAN; }
bool wifiPetListening() { return s_phase == PHASE_LISTEN; }
bool wifiPetBusy() { return s_phase == PHASE_SCAN || s_phase == PHASE_LISTEN; }

const WifiPetStats& wifiPetStats() { return s_stats; }

uint8_t wifiPetChannel() { return kHop[s_hopIdx % (sizeof(kHop) / sizeof(kHop[0]))]; }
