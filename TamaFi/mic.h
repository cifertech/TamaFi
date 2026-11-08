#pragma once

#include <Arduino.h>

bool micInit();
bool micOk();
void micUpdate();

float micLevel();

bool micHearing();

void micHoldoff(uint32_t extraMs);
bool micHeldOff();
