#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

enum StatusPage : uint8_t {
  STATUS_MENU = 0,
  STATUS_PET,
  STATUS_ENV,
  STATUS_SYS,
  STATUS_CTL,
  STATUS_SET,
  STATUS_LOG,
  STATUS_STAMPS
};

void statusMenuInit(TFT_eSPI& tft);
void statusMenuOpenMain();
void statusMenuOpenQuick(StatusPage page);
StatusPage statusMenuPage();
bool statusMenuHandleButton(int pin);
void statusMenuTick();
const char* statusMenuTitle();
bool statusMenuAsleep();
bool statusMenuWakeIfAsleep();
void statusMenuPowerTick();
void statusMenuSyncPet();
void statusMenuSave();
bool statusMenuSleepEnabled();
void statusMenuSetSleep(bool on);
const char* statusMenuThemeName();
uint8_t statusMenuThemeIndex();
bool statusMenuSetTheme(uint8_t index);
bool statusMenuSetThemeName(const char* name);
uint16_t statusMenuSaveIntervalMs();
void statusMenuSetSaveIntervalMs(uint16_t ms);
uint8_t statusMenuTftBrightness();
void statusMenuSetTftBrightness(uint8_t index);
const char* statusMenuMoodLabel();
