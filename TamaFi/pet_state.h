#pragma once

#include <Arduino.h>
#include "face.h"

struct PetSignalStats {
  int count;
  int strongCount;
  int hiddenCount;
  int openCount;
  int avgRSSI;
  uint16_t deauth;
  uint16_t probe;
  uint8_t threat;
};

enum PetStage : uint8_t {
  PET_STAGE_BABY = 0,
  PET_STAGE_TEEN,
  PET_STAGE_ADULT,
  PET_STAGE_ELDER
};

enum PetActivity : uint8_t {
  PET_ACTIVITY_IDLE = 0,
  PET_ACTIVITY_HUNT,
  PET_ACTIVITY_DISCOVER,
  PET_ACTIVITY_REST
};

struct PetSnapshot {
  uint8_t hunger;
  uint8_t energy;
  uint8_t happiness;
  uint8_t health;
  uint8_t curiosity;
  uint8_t comfort;
  uint32_t ageMinutes;
  PetStage stage;
  PetActivity activity;
};

struct PetAction {
  TamafiMood mood;
  uint16_t holdMs;
  const char* label;
};

bool petStateInit();
void petStateUpdate();
void petStateApplyWifi(const PetSignalStats& stats);
void petStateApplyBle(const PetSignalStats& stats);
bool petStateConsumeAction(PetAction& action);
bool petStateHasNeed();
TamafiMood petStateBaselineMood();
bool petStateLook(float& x, float& y);
void petStateOnPetted();
void petStateOnHeard();
void petStateOnShaken();
void petStateOnNudged();
void petStateOnTiltHeld();
void petStateReset(bool fullReset);
void petStateSetSaveIntervalMs(uint32_t ms);
bool petStateHatched();
void petStateMarkHatched();
const PetSnapshot& petStateSnapshot();
const PetSignalStats& petStateWifi();
const PetSignalStats& petStateBle();
const char* petStageName(PetStage stage);
const char* petActivityName(PetActivity activity);
