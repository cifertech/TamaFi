#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#define TAMAFI_FACE_SIZE 240

enum TamafiMood : uint8_t {
  TAMAFI_MOOD_IDLE = 0,
  TAMAFI_MOOD_HAPPY,
  TAMAFI_MOOD_SAD,
  TAMAFI_MOOD_ANGRY,
  TAMAFI_MOOD_SURPRISED,
  TAMAFI_MOOD_LOVE,
  TAMAFI_MOOD_CONFUSED,
  TAMAFI_MOOD_BORED,
  TAMAFI_MOOD_SCARED,
  TAMAFI_MOOD_EXCITED,
  TAMAFI_MOOD_SHY,
  TAMAFI_MOOD_LAUGH,
  TAMAFI_MOOD_WINK,
  TAMAFI_MOOD_DIZZY,
  TAMAFI_MOOD_CRY,
  TAMAFI_MOOD_SLEEP,
  TAMAFI_MOOD_LISTEN,
  TAMAFI_MOOD_THINK,
  TAMAFI_MOOD_SPEAK,
  TAMAFI_MOOD_BLINK,
  TAMAFI_MOOD_AIR_ATTACK,
  TAMAFI_MOOD_AIR_NOISE,
  TAMAFI_MOOD_AIR_OPEN,
  TAMAFI_MOOD_COUNT
};

void faceInit(TFT_eSPI& tft);
void faceSetMood(TamafiMood mood);
void faceSetLook(float x, float y);
void faceClearLook();
bool faceBusy();
bool faceTick(bool push = true);
TamafiMood faceMood();
const char* faceMoodName(TamafiMood mood);
TamafiMood faceMoodFromName(const char* name);
TFT_eSprite* faceSprite();
void facePresentSprite();
void faceSay(const char* text, uint16_t holdMs);
void faceClearSay();
void faceHoldForHatch();
void faceDrawForHatch(float scale, float lids, float faceY, float gazeX);
void faceBeginLife();
