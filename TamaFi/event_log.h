#pragma once

#include <Arduino.h>

enum LogKind : uint8_t {
  LOG_PET = 0,
  LOG_HUNT,
  LOG_DISC,
  LOG_REST,
  LOG_AIR,
  LOG_WIFI,
  LOG_BLE,
  LOG_IMU,
  LOG_MIC,
  LOG_SYS
};

struct LogEntry {
  uint32_t ms;
  LogKind kind;
  char text[22];
};

void eventLogPush(LogKind kind, const char* text);
void eventLogPushf(LogKind kind, const char* fmt, ...);
uint8_t eventLogCount();
uint32_t eventLogSeq();
const LogEntry* eventLogAt(uint8_t age);
const char* eventLogKindName(LogKind kind);
uint16_t eventLogKindColor(LogKind kind);
