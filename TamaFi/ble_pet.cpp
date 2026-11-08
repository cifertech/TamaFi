#include "config.h"
#include "ble_pet.h"

#if TAMAFI_BLE_PET_ENABLE
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <esp_bt.h>
#endif

static const uint32_t kFirstBleScanMs = 15000;
static const uint32_t kBleScanIntervalMs = 45000;
static const uint32_t kBleScanSeconds = 3;

static bool s_ok = false;
static bool s_scanning = false;
static bool s_hasStats = false;
static uint32_t s_nextScanAt = 0;
static BlePetStats s_stats = {0, 0, -100};

#if TAMAFI_BLE_PET_ENABLE
static BLEScan* s_scan = nullptr;
static volatile bool s_scanDone = false;
static volatile int s_count = 0;
static volatile int s_strong = 0;
static volatile int s_totalRSSI = 0;

class TamafiBleCallbacks : public BLEAdvertisedDeviceCallbacks {
 public:
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    const int rssi = advertisedDevice.getRSSI();
    s_count++;
    s_totalRSSI += rssi;
    if (rssi > -65) s_strong++;
  }
};

static TamafiBleCallbacks s_callbacks;

static void onBleScanDone(BLEScanResults) { s_scanDone = true; }

static void finishScan(uint32_t now) {
  s_scanning = false;
  if (!s_scanDone && s_scan) s_scan->stop();
  const int count = s_count;
  s_stats.deviceCount = count;
  s_stats.strongCount = s_strong;
  s_stats.avgRSSI = count > 0 ? s_totalRSSI / count : -100;
  s_hasStats = true;
  s_nextScanAt = now + kBleScanIntervalMs;
  if (s_scan) s_scan->clearResults();
  Serial.printf("BLE pet: devices=%d strong=%d avg=%d\n", s_stats.deviceCount,
                s_stats.strongCount, s_stats.avgRSSI);
}
#endif

bool blePetInit() {
#if !TAMAFI_BLE_PET_ENABLE
  Serial.println("BLE pet: disabled");
  return false;
#else
  if (ESP.getFreeHeap() < TAMAFI_BLE_MIN_FREE_HEAP) {
    Serial.printf("BLE pet: skipped, heap=%u\n", ESP.getFreeHeap());
    return false;
  }

  esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  BLEDevice::init("");
  s_scan = BLEDevice::getScan();
  if (!s_scan) {
    Serial.println("BLE pet: scan unavailable");
    return false;
  }

  s_scan->setAdvertisedDeviceCallbacks(&s_callbacks, false, false);
  s_scan->setActiveScan(false);
  s_scan->setInterval(320);
  s_scan->setWindow(120);

  s_ok = true;
  s_nextScanAt = millis() + kFirstBleScanMs;
  Serial.println("BLE pet: passive scan mode");
  return true;
#endif
}

void blePetUpdate(bool radioFree) {
#if TAMAFI_BLE_PET_ENABLE
  if (!s_ok || !s_scan) return;
  const uint32_t now = millis();

  if (s_scanning) {
    if (s_scanDone || now >= s_nextScanAt) finishScan(now);
    return;
  }

  if (!radioFree || now < s_nextScanAt) return;
  if (ESP.getFreeHeap() < TAMAFI_BLE_MIN_FREE_HEAP) {
    s_nextScanAt = now + kBleScanIntervalMs;
    Serial.printf("BLE pet: delayed, heap=%u\n", ESP.getFreeHeap());
    return;
  }

  s_count = 0;
  s_strong = 0;
  s_totalRSSI = 0;
  s_scanDone = false;
  if (s_scan->start(kBleScanSeconds, onBleScanDone, false)) {
    s_scanning = true;
    s_nextScanAt = now + (kBleScanSeconds + 2) * 1000UL;
    Serial.println("BLE pet: scan start");
  } else {
    s_nextScanAt = now + kBleScanIntervalMs;
    Serial.println("BLE pet: scan start failed");
  }
#else
  (void)radioFree;
#endif
}

bool blePetConsumeStats(BlePetStats& stats) {
  if (!s_hasStats) return false;
  stats = s_stats;
  s_hasStats = false;
  return true;
}

bool blePetScanning() { return s_scanning; }

bool blePetOk() { return s_ok; }

const BlePetStats& blePetStats() { return s_stats; }
