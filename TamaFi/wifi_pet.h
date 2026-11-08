#pragma once

#include <Arduino.h>
#include "face.h"

enum AirThreat : uint8_t {
  AIR_CALM = 0,
  AIR_BUSY,
  AIR_OPEN,
  AIR_NOISY,
  AIR_HOSTILE
};

struct WifiPetStats {
  int netCount;
  int strongCount;
  int hiddenCount;
  int openCount;
  int avgRSSI;
  uint16_t deauth;
  uint16_t probe;
  AirThreat threat;
};

bool wifiPetInit();
void wifiPetUpdate(bool radioFree = true);
bool wifiPetConsumeStats(WifiPetStats& stats);
bool wifiPetScanning();
bool wifiPetListening();
bool wifiPetBusy();
const WifiPetStats& wifiPetStats();
uint8_t wifiPetChannel();
const char* airThreatName(AirThreat threat);

