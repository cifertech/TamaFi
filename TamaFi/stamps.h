#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

enum StampId : uint8_t {
  STAMP_ONLINE = 0,
  STAMP_HUNTER,
  STAMP_NAP,
  STAMP_GROWN,
  STAMP_HOSTILE,
  STAMP_STATIC,
  STAMP_HEARD,
  STAMP_CROWD,
  STAMP_OPEN,
  STAMP_DAY,
  STAMP_WEEK,
  STAMP_COUNT
};

void stampsInit();
void stampsNote(StampId id);
void stampsTick(bool canShow);
bool stampsBusy();
bool stampsWantsOverlay();
bool stampsDismiss();
bool stampsHas(StampId id);
uint8_t stampsUnlockedCount();
const char* stampsTitle(StampId id);
void stampsDrawMark(TFT_eSPI& g, StampId id, int cx, int cy, int size, uint16_t color);
