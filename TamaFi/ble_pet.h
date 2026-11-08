#pragma once

#include <Arduino.h>

struct BlePetStats {
  int deviceCount;
  int strongCount;
  int avgRSSI;
};

bool blePetInit();
void blePetUpdate(bool radioFree);
bool blePetConsumeStats(BlePetStats& stats);
bool blePetScanning();
bool blePetOk();
const BlePetStats& blePetStats();
