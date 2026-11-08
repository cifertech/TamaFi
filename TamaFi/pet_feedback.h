#pragma once

#include <Arduino.h>
#include "face.h"

bool petFeedbackInit();
void petFeedbackUpdate();
void petFeedbackEvent(const char* label, TamafiMood mood);
void petFeedbackSetSoundEnabled(bool enabled);
void petFeedbackSetLedsEnabled(bool enabled);
void petFeedbackSetLedBrightness(uint8_t index);
bool petFeedbackSoundEnabled();
bool petFeedbackLedsEnabled();
uint8_t petFeedbackLedBrightness();
bool petFeedbackSpeaking();
