#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

void hatchBlitClosed(TFT_eSprite& d, int ox, int oy, uint16_t color);
void hatchBlitTop(TFT_eSprite& d, int ox, int oy, uint16_t color);
void hatchBlitBottom(TFT_eSprite& d, int ox, int oy, uint16_t color);
void hatchBlitShell(TFT_eSprite& d, int ox, int oy, int split, int open, float crackLen, uint16_t color);
int hatchCrackMid(int x);
int hatchEggW();
int hatchEggH();
int hatchEggX();
int hatchEggY();
int hatchCrackTop(int x);
int hatchCrackBot(int x);
