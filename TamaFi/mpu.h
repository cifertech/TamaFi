#pragma once

#include <Arduino.h>

enum TamafiGesture : uint8_t {
  TAMAFI_GESTURE_NONE = 0,
  TAMAFI_GESTURE_NUDGE,
  TAMAFI_GESTURE_SHAKE,
  TAMAFI_GESTURE_TAP,
  TAMAFI_GESTURE_TILT_HOLD,
  TAMAFI_GESTURE_FLIP
};

bool mpuInit();
bool mpuOk();
void mpuUpdate();

float mpuTiltX();
float mpuTiltY();

TamafiGesture mpuConsumeGesture();
const char* mpuGestureName(TamafiGesture gesture);

bool mpuConsumeShake();
bool mpuConsumeTap();
bool mpuUpsideDown();
