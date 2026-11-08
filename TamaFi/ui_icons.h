#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

enum UiIcon : uint8_t {
  UI_ICON_PET = 0,
  UI_ICON_ENV = 1,
  UI_ICON_CPU = 2,
  UI_ICON_SLIDERS = 3,
  UI_ICON_GEAR = 4,
  UI_ICON_LOG = 5,
  UI_ICON_STAMP = 6,
  UI_ICON_BACK = 7,
  UI_ICON_SUN = 8,
  UI_ICON_LED = 9,
  UI_ICON_MIC = 10,
  UI_ICON_PIXELS = 11,
  UI_ICON_PALETTE = 12,
  UI_ICON_SLEEP = 13,
  UI_ICON_SAVE = 14,
  UI_ICON_RECYCLE = 15,
  UI_ICON_NUKE = 16,
  UI_ICON_WIFI = 17,
  UI_ICON_SEARCH = 18,
  UI_ICON_DINO = 19,
  UI_ICON_SKULL = 20,
  UI_ICON_NOISE = 21,
  UI_ICON_CROWD = 22,
  UI_ICON_UNLOCK = 23,
  UI_ICON_HOURGLASS = 24,
  UI_ICON_SWORD = 25,
  UI_ICON_COUNT
};

void uiDrawIconAt(TFT_eSPI& g, uint8_t id, int x, int y, int size, uint16_t color);

void uiDrawIconCentered(TFT_eSPI& g, uint8_t id, int cx, int cy, int size, uint16_t color);

